#include <cstddef>
#include <thread>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/time.h>
}

#include "log.h"
#include "scoped_exit.h"
#include "audio_output.h"
#include "video_decoder.h"

static double get_sys_time_sec() { return static_cast<double>(av_gettime_relative()) / 1000000.0; }

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
    AVPacket *pkt = nullptr;

    DEFER({
        free_resources();
        if (frame)
            av_frame_free(&frame);
        if (pkt)
            av_packet_free(&pkt);
        if (fmt_ctx)
            avformat_close_input(&fmt_ctx);
        LOG_INFO("Decoder thread finished.");
    });

    LOG_INFO("Opening file: {}", file_.toStdString());

    if (avformat_open_input(&fmt_ctx, file_.toStdString().c_str(), nullptr, nullptr) != 0)
    {
        LOG_ERROR("Failed to open input file");
        return;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0)
    {
        LOG_ERROR("Failed to find stream info");
        return;
    }

    if (!init_video_decoder(fmt_ctx))
    {
        LOG_ERROR("Failed to init video decoder");
        return;
    }

    init_audio_decoder(fmt_ctx);

    if (audio_index_ == -1)
    {
        video_start_system_time_ = av_gettime_relative();
        LOG_INFO("No audio stream found, using system clock.");
    }

    frame = av_frame_alloc();
    pkt = av_packet_alloc();

    LOG_INFO("Start decoding loop...");

    while (!stop_)
    {
        if (audio_index_ != -1)
        {
            double duration = audio_out_->get_buffer_duration();

            if (duration > 0.5)
            {
                QThread::msleep(10);
                continue;
            }
        }

        if (av_read_frame(fmt_ctx, pkt) < 0)
        {
            LOG_INFO("EOF or Read Error. Looping...");
            if (audio_index_ == -1)
            {
                video_start_system_time_ = av_gettime_relative();
            }
            av_seek_frame(fmt_ctx, -1, 0, AVSEEK_FLAG_BACKWARD);
            continue;
        }
        DEFER(av_packet_unref(pkt));

        if (pkt->stream_index == video_index_)
        {
            process_video_packet(pkt, frame);
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

    video_stream_ = fmt_ctx->streams[video_index_];
    AVCodecParameters *par = video_stream_->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (codec == nullptr)
    {
        return false;
    }

    video_ctx_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(video_ctx_, par);
    return avcodec_open2(video_ctx_, codec, nullptr) >= 0;
}

bool video_decoder::init_audio_decoder(AVFormatContext *fmt_ctx)
{
    audio_index_ = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_index_ < 0)
    {
        return false;
    }

    audio_stream_ = fmt_ctx->streams[audio_index_];
    AVCodecParameters *par = audio_stream_->codecpar;
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
    if (AV_CHANNEL_ORDER_UNSPEC == in_layout.order)
    {
        av_channel_layout_default(&in_layout, audio_ctx_->ch_layout.nb_channels);
    }

    AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
    swr_ctx_ = nullptr;
    swr_alloc_set_opts2(&swr_ctx_, &out_layout, AV_SAMPLE_FMT_S16, 44100, &in_layout, audio_ctx_->sample_fmt, audio_ctx_->sample_rate, 0, nullptr);
    swr_init(swr_ctx_);

    return audio_out_->start(44100, 2);
}

double video_decoder::get_master_clock()
{
    if (audio_index_ != -1)
    {
        return audio_out_->get_current_time();
    }
    return get_sys_time_sec() - (static_cast<double>(video_start_system_time_) / 1000000.0);
}

void video_decoder::process_video_packet(AVPacket *pkt, AVFrame *frame)
{
    int ret = avcodec_send_packet(video_ctx_, pkt);
    if (ret < 0)
    {
        LOG_ERROR("Video send packet error: {}", ret);
        return;
    }

    while (avcodec_receive_frame(video_ctx_, frame) == 0)
    {
        double pts = 0;
        if (frame->pts != AV_NOPTS_VALUE)
        {
            pts = static_cast<double>(frame->pts) * av_q2d(video_stream_->time_base);
        }
        else
        {
            pts = static_cast<double>(frame->best_effort_timestamp) * av_q2d(video_stream_->time_base);
        }

        double master_time = get_master_clock();
        double diff = pts - master_time;

        if (diff < -0.06)
        {
            LOG_WARN("[Drop Frame] PTS: {:.3f} Master: {:.3f} Diff: {:.3f}", pts, master_time, diff);
            continue;
        }

        synchronize_video(pts);

        if (stop_)
        {
            break;
        }

        auto vframe = std::make_shared<video_frame>();
        vframe->width = frame->width;
        vframe->height = frame->height;
        vframe->pts = pts;

        auto y_size = static_cast<size_t>(frame->width) * static_cast<size_t>(frame->height);
        size_t uv_size = y_size / 4;

        vframe->y_data.resize(y_size);
        vframe->u_data.resize(uv_size);
        vframe->v_data.resize(uv_size);
        vframe->y_line_size = frame->width;
        vframe->u_line_size = frame->width / 2;
        vframe->v_line_size = frame->width / 2;

        for (int i = 0; i < frame->height; ++i)
        {
            memcpy(vframe->y_data.data() + static_cast<ptrdiff_t>(i * vframe->y_line_size),
                   frame->data[0] + static_cast<ptrdiff_t>(i * frame->linesize[0]),
                   static_cast<size_t>(vframe->y_line_size));
        }
        for (int i = 0; i < frame->height / 2; ++i)
        {
            memcpy(vframe->u_data.data() + static_cast<ptrdiff_t>(i * vframe->u_line_size),
                   frame->data[1] + static_cast<ptrdiff_t>(i * frame->linesize[1]),
                   static_cast<size_t>(vframe->u_line_size));
        }
        for (int i = 0; i < frame->height / 2; ++i)
        {
            memcpy(vframe->v_data.data() + static_cast<ptrdiff_t>(i * vframe->v_line_size),
                   frame->data[2] + static_cast<ptrdiff_t>(i * frame->linesize[2]),
                   static_cast<size_t>(vframe->v_line_size));
        }

        emit frame_ready(vframe);
    }
}

void video_decoder::process_audio_packet(AVPacket *pkt, AVFrame *frame)
{
    int ret = avcodec_send_packet(audio_ctx_, pkt);
    if (ret < 0)
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
            size_t actual_size = static_cast<size_t>(converted_samples) * 2 * 2;
            pcm_buffer.resize(actual_size);

            double pts = 0;
            if (frame->pts != AV_NOPTS_VALUE)
            {
                pts = static_cast<double>(frame->pts) * av_q2d(audio_stream_->time_base);
            }

            audio_out_->write(pcm_buffer, pts);
        }
    }
}

void video_decoder::synchronize_video(double pts)
{
    constexpr double kSyncThreshold = 0.01;

    while (!stop_)
    {
        double current_time = get_master_clock();
        double diff = pts - current_time;

        if (diff < kSyncThreshold)
        {
            break;
        }

        LOG_TRACE("Sync wait. PTS: {:.3f} Master: {:.3f} Diff: {:.3f}", pts, current_time, diff);

        if (diff > 0.010)
        {
            av_usleep(2000);
        }
        else
        {
            std::this_thread::yield();
        }
    }
}

void video_decoder::free_resources()
{
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
