#include "log.h"
#include "scoped_exit.h"
#include "video_decoder.h"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

video_decoder::video_decoder(QObject *parent) : QThread(parent), stop_(false) {}

video_decoder::~video_decoder()
{
    stop();
    wait();
}

bool video_decoder::open(const QString &file_path)
{
    stop();
    file_ = file_path;
    stop_ = false;
    start();
    return true;
}

void video_decoder::stop() { stop_ = true; }

void video_decoder::run()
{
    LOG_INFO("decoder thread started {}", file_.toStdString());

    AVFormatContext *fmt_ctx = nullptr;
    AVCodecContext *codec_ctx = nullptr;
    SwsContext *sws_ctx = nullptr;
    AVFrame *frame = nullptr;
    AVFrame *frame_rgb = nullptr;
    AVPacket *pkt = nullptr;
    uint8_t *buffer = nullptr;

    DEFER({
        LOG_INFO("Cleaning up decoder resources...");
        if (buffer)
            av_free(buffer);
        if (frame_rgb)
            av_frame_free(&frame_rgb);
        if (frame)
            av_frame_free(&frame);
        if (pkt)
            av_packet_free(&pkt);
        if (sws_ctx)
            sws_freeContext(sws_ctx);
        if (codec_ctx)
            avcodec_free_context(&codec_ctx);
        if (fmt_ctx)
            avformat_close_input(&fmt_ctx);
    });

    if (avformat_open_input(&fmt_ctx, file_.toStdString().c_str(), nullptr, nullptr) != 0)
    {
        LOG_ERROR("failed to open video file {}", file_.toStdString());
        return;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0)
    {
        LOG_ERROR("failed to find stream info {}", file_.toStdString());
        return;
    }

    int video_index = -1;
    for (int32_t i = 0; i < fmt_ctx->nb_streams; i++)
    {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_index = i;
            break;
        }
    }

    if (video_index == -1)
    {
        LOG_ERROR("no video stream found from {}", file_.toStdString());
        return;
    }

    AVCodecParameters *codec_parame = fmt_ctx->streams[video_index]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codec_parame->codec_id);
    if (codec == nullptr)
    {
        LOG_ERROR("decoder not found");
        return;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, codec_parame);

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0)
    {
        LOG_ERROR("failed to open codec");
        return;
    }

    sws_ctx = sws_getContext(codec_ctx->width,
                             codec_ctx->height,
                             codec_ctx->pix_fmt,
                             codec_ctx->width,
                             codec_ctx->height,
                             AV_PIX_FMT_RGB24,
                             SWS_BILINEAR,
                             nullptr,
                             nullptr,
                             nullptr);

    frame = av_frame_alloc();
    frame_rgb = av_frame_alloc();
    pkt = av_packet_alloc();

    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);
    buffer = static_cast<uint8_t *>(av_malloc(static_cast<size_t>(num_bytes)));

    av_image_fill_arrays(frame_rgb->data, frame_rgb->linesize, buffer, AV_PIX_FMT_RGB24, codec_ctx->width, codec_ctx->height, 1);

    LOG_INFO("decoder ready size {}x{}", codec_ctx->width, codec_ctx->height);

    while (!stop_)
    {
        if (av_read_frame(fmt_ctx, pkt) < 0)
        {
            LOG_INFO("end of file or error");
            break;
        }

        DEFER(av_packet_unref(pkt));

        if (pkt->stream_index != video_index)
        {
            continue;
        }
        if (avcodec_send_packet(codec_ctx, pkt) != 0)
        {
            continue;
        }
        while (avcodec_receive_frame(codec_ctx, frame) == 0)
        {
            sws_scale(sws_ctx, (const uint8_t *const *)frame->data, frame->linesize, 0, codec_ctx->height, frame_rgb->data, frame_rgb->linesize);

            QImage img(static_cast<uchar *>(buffer), codec_ctx->width, codec_ctx->height, QImage::Format_RGB888);

            emit frame_ready(img.copy());

            QThread::msleep(33);
        }
    }
}

void video_decoder::clear() {}
