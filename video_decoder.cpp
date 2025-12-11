#include "scoped_exit.h"
#include "audio_output.h"
#include "video_decoder.h"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

video_decoder::video_decoder(QObject *parent) : QThread(parent), stop_(false) { audio_out_ = std::make_unique<audio_output>(); }

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

void video_decoder::stop()
{
    stop_ = true;
    audio_out_->stop();
}

void video_decoder::run()
{
    AVFormatContext *fmt_ctx = nullptr;
    AVFrame *frame = nullptr;
    AVFrame *frame_rgb = nullptr;
    AVPacket *pkt = nullptr;
    uint8_t *video_buffer = nullptr;

    DEFER({
        free_resources();
        if (video_buffer)
            av_free(video_buffer);
        if (frame_rgb)
            av_frame_free(&frame_rgb);
        if (frame)
            av_frame_free(&frame);
        if (pkt)
            av_packet_free(&pkt);
        if (fmt_ctx)
            avformat_close_input(&fmt_ctx);
    });

    if (avformat_open_input(&fmt_ctx, file_.toStdString().c_str(), nullptr, nullptr) != 0)
    {
        return;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0)
    {
        return;
    }

    if (init_video_decoder(fmt_ctx))
    {
        int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, video_ctx_->width, video_ctx_->height, 1);
        video_buffer = static_cast<uint8_t *>(av_malloc(static_cast<size_t>(num_bytes)));
        frame_rgb = av_frame_alloc();
        av_image_fill_arrays(frame_rgb->data, frame_rgb->linesize, video_buffer, AV_PIX_FMT_RGB24, video_ctx_->width, video_ctx_->height, 1);
    }

    init_audio_decoder(fmt_ctx);

    frame = av_frame_alloc();
    pkt = av_packet_alloc();

    while (!stop_)
    {
        if (audio_index_ != -1 && audio_out_->buffer_size() > 10)
        {
            QThread::msleep(10);
            continue;
        }

        if (av_read_frame(fmt_ctx, pkt) < 0)
        {
            break;
        }
        DEFER(av_packet_unref(pkt));

        if (pkt->stream_index == video_index_)
        {
            process_video_packet(pkt, frame, frame_rgb, video_buffer);
        }
        else if (pkt->stream_index == audio_index_)
        {
            process_audio_packet(pkt, frame);
        }
    }
}

bool video_decoder::init_video_decoder(AVFormatContext *fmt_ctx)
{
    video_index_ = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_index_ < 0)
    {
        return false;
    }

    AVCodecParameters *par = fmt_ctx->streams[video_index_]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (codec == nullptr)
    {
        return false;
    }

    video_ctx_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(video_ctx_, par);

    if (avcodec_open2(video_ctx_, codec, nullptr) < 0)
    {
        return false;
    }

    sws_ctx_ = sws_getContext(video_ctx_->width,
                              video_ctx_->height,
                              video_ctx_->pix_fmt,
                              video_ctx_->width,
                              video_ctx_->height,
                              AV_PIX_FMT_RGB24,
                              SWS_BILINEAR,
                              nullptr,
                              nullptr,
                              nullptr);
    return true;
}

bool video_decoder::init_audio_decoder(AVFormatContext *fmt_ctx)
{
    audio_index_ = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_index_ < 0)
    {
        return false;
    }

    AVCodecParameters *par = fmt_ctx->streams[audio_index_]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (codec == nullptr)
    {
        return false;
    }

    audio_ctx_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(audio_ctx_, par);

    if (avcodec_open2(audio_ctx_, codec, nullptr) < 0)
    {
        return false;
    }

    AVChannelLayout in_layout = audio_ctx_->ch_layout;

    if (in_layout.order == AV_CHANNEL_ORDER_UNSPEC)
    {
        av_channel_layout_default(&in_layout, audio_ctx_->ch_layout.nb_channels);
    }

    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;

    swr_ctx_ = nullptr;
    int ret = swr_alloc_set_opts2(
        &swr_ctx_, &out_layout, AV_SAMPLE_FMT_S16, 44100, &in_layout, audio_ctx_->sample_fmt, audio_ctx_->sample_rate, 0, nullptr);

    if (in_layout.order != AV_CHANNEL_ORDER_UNSPEC && av_channel_layout_compare(&in_layout, &audio_ctx_->ch_layout) != 0)
    {
        av_channel_layout_uninit(&in_layout);
    }

    if (ret < 0 || swr_init(swr_ctx_) < 0)
    {
        swr_free(&swr_ctx_);
        return false;
    }

    return audio_out_->start(44100, 2);
}

void video_decoder::process_video_packet(AVPacket *pkt, AVFrame *frame, AVFrame *frame_rgb, uint8_t *buffer)
{
    if (avcodec_send_packet(video_ctx_, pkt) != 0)
    {
        return;
    }

    while (avcodec_receive_frame(video_ctx_, frame) == 0)
    {
        sws_scale(sws_ctx_, frame->data, frame->linesize, 0, video_ctx_->height, frame_rgb->data, frame_rgb->linesize);

        QImage img(static_cast<uchar *>(buffer), video_ctx_->width, video_ctx_->height, QImage::Format_RGB888);
        emit frame_ready(img.copy());

        if (audio_index_ == -1)
        {
            QThread::msleep(33);
        }
    }
}

void video_decoder::process_audio_packet(AVPacket *pkt, AVFrame *frame)
{
    if (avcodec_send_packet(audio_ctx_, pkt) != 0)
    {
        return;
    }

    while (avcodec_receive_frame(audio_ctx_, frame) == 0)
    {
        int out_samples = (44100 * frame->nb_samples / audio_ctx_->sample_rate) + 256;
        int out_bytes = av_samples_get_buffer_size(nullptr, 2, out_samples, AV_SAMPLE_FMT_S16, 1);

        std::vector<uint8_t> pcm_buffer(static_cast<size_t>(out_bytes));
        uint8_t *out_data[1] = {pcm_buffer.data()};

        int converted_samples = swr_convert(swr_ctx_, out_data, out_samples, frame->data, frame->nb_samples);

        if (converted_samples > 0)
        {
            int actual_size = converted_samples * 2 * 2;
            pcm_buffer.resize(static_cast<size_t>(actual_size));
            audio_out_->write(pcm_buffer);
        }
    }
}

void video_decoder::free_resources()
{
    if (sws_ctx_ != nullptr)
    {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    if (swr_ctx_ != nullptr)
    {
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
    }
    if (video_ctx_ != nullptr)
    {
        avcodec_free_context(&video_ctx_);
        video_ctx_ = nullptr;
    }
    if (audio_ctx_ != nullptr)
    {
        avcodec_free_context(&audio_ctx_);
        audio_ctx_ = nullptr;
    }
}
