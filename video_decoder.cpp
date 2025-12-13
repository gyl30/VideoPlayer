#include "log.h"
#include "scoped_exit.h"
#include "audio_output.h"
#include "video_decoder.h"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
}

video_decoder::video_decoder(QObject *parent) : QObject(parent) { audio_out_ = std::make_unique<audio_output>(); }

video_decoder::~video_decoder() { stop(); }

bool video_decoder::open(const QString &file_path)
{
    stop();
    file_ = file_path;
    stop_ = false;

    video_packet_queue_.start();
    audio_packet_queue_.start();
    video_frame_queue_.start();

    demux_thread_ = std::thread(&video_decoder::demux_thread_func, this);
    return true;
}

void video_decoder::stop()
{
    stop_ = true;

    video_packet_queue_.abort();
    audio_packet_queue_.abort();
    video_frame_queue_.abort();

    if (demux_thread_.joinable())
    {
        demux_thread_.join();
    }
    if (video_thread_.joinable())
    {
        video_thread_.join();
    }
    if (audio_thread_.joinable())
    {
        audio_thread_.join();
    }

    audio_out_->stop();
    free_resources();
}

void video_decoder::demux_thread_func()
{
    AVFormatContext *fmt_ctx = nullptr;
    AVPacket *pkt = nullptr;

    if (avformat_open_input(&fmt_ctx, file_.toStdString().c_str(), nullptr, nullptr) != 0)
    {
        LOG_ERROR("Failed to open input file");
        return;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0)
    {
        LOG_ERROR("Failed to find stream info");
        avformat_close_input(&fmt_ctx);
        return;
    }

    if (init_video_decoder(fmt_ctx))
    {
        video_thread_ = std::thread(&video_decoder::video_thread_func, this);
    }

    if (init_audio_decoder(fmt_ctx))
    {
        audio_thread_ = std::thread(&video_decoder::audio_thread_func, this);
    }

    pkt = av_packet_alloc();

    while (!stop_)
    {
        if (video_packet_queue_.size() > 15 * 1024 * 1024 || audio_packet_queue_.size() > 5 * 1024 * 1024)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int ret = av_read_frame(fmt_ctx, pkt);
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            break;
        }

        if (pkt->stream_index == video_index_)
        {
            AVPacket *clone = av_packet_clone(pkt);
            video_packet_queue_.push(clone);
        }
        else if (pkt->stream_index == audio_index_)
        {
            AVPacket *clone = av_packet_clone(pkt);
            audio_packet_queue_.push(clone);
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx);
}

void video_decoder::video_thread_func()
{
    AVFrame *frame = av_frame_alloc();

    while (!stop_)
    {
        AVPacket *pkt = video_packet_queue_.pop();
        if (pkt == nullptr)
        {
            break;
        }

        DEFER(av_packet_free(&pkt));

        if (avcodec_send_packet(video_ctx_, pkt) < 0)
        {
            continue;
        }

        while (avcodec_receive_frame(video_ctx_, frame) == 0)
        {
            double pts = static_cast<double>(frame->best_effort_timestamp) * av_q2d(video_stream_->time_base);
            double duration = static_cast<double>(frame->duration) * av_q2d(video_stream_->time_base);

            auto vframe = video_frame::make(frame, pts, duration);
            video_frame_queue_.push(vframe);
        }
    }

    av_frame_free(&frame);
}

void video_decoder::audio_thread_func()
{
    AVFrame *frame = av_frame_alloc();

    while (!stop_)
    {
        AVPacket *pkt = audio_packet_queue_.pop();
        if (pkt == nullptr)
        {
            break;
        }

        DEFER(av_packet_free(&pkt));

        if (avcodec_send_packet(audio_ctx_, pkt) < 0)
        {
            continue;
        }

        while (avcodec_receive_frame(audio_ctx_, frame) == 0)
        {
            int out_samples = (44100 * frame->nb_samples / audio_ctx_->sample_rate) + 256;
            int out_bytes = av_samples_get_buffer_size(nullptr, 2, out_samples, AV_SAMPLE_FMT_S16, 1);

            static std::vector<uint8_t> pcm_buffer;
            if (pcm_buffer.capacity() < static_cast<size_t>(out_bytes))
            {
                pcm_buffer.reserve(static_cast<size_t>(out_bytes) * 2);
            }
            pcm_buffer.resize(static_cast<size_t>(out_bytes));

            uint8_t *out_data[1] = {pcm_buffer.data()};
            int converted_samples = swr_convert(swr_ctx_, out_data, out_samples, (const uint8_t **)frame->data, frame->nb_samples);

            if (converted_samples > 0)
            {
                size_t actual_size = static_cast<size_t>(converted_samples) * 2 * 2;
                double pts = static_cast<double>(frame->pts) * av_q2d(audio_stream_->time_base);
                audio_out_->set_clock_at(pts, 0);
                audio_out_->write(pcm_buffer.data(), actual_size);
            }
        }
    }

    av_frame_free(&frame);
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
    swr_alloc_set_opts2(&swr_ctx_, &out_layout, AV_SAMPLE_FMT_S16, 44100, &in_layout, audio_ctx_->sample_fmt, audio_ctx_->sample_rate, 0, nullptr);
    swr_init(swr_ctx_);

    return audio_out_->start(44100, 2);
}

VideoFramePtr video_decoder::get_video_frame() { return video_frame_queue_.peek(); }

void video_decoder::pop_video_frame() { video_frame_queue_.pop(); }

double video_decoder::get_master_clock() { return audio_out_->get_current_time(); }

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
