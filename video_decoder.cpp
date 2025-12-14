#include "video_decoder.h"
#include "log.h"
#include "scoped_exit.h"
#include "audio_output.h"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>

#include <algorithm>
}

int video_decoder::decode_interrupt_cb(void *ctx)
{
    auto *decoder = static_cast<video_decoder *>(ctx);
    return decoder->is_stopping() ? 1 : 0;
}

static enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;
    auto *decoder = static_cast<video_decoder *>(ctx->opaque);

    for (p = pix_fmts; *p != -1; p++)
    {
        if (*p == static_cast<enum AVPixelFormat>(decoder->current_hw_pix_fmt()))
        {
            return *p;
        }
    }
    return AV_PIX_FMT_NONE;
}

video_decoder::video_decoder(QObject *parent) : QObject(parent) { audio_out_ = std::make_unique<audio_output>(); }

video_decoder::~video_decoder() { stop(); }

bool video_decoder::open(const QString &file_path)
{
    stop();
    file_ = file_path;
    abort_request_ = false;
    seek_req_ = false;
    video_ctx_serial_ = -1;
    audio_ctx_serial_ = -1;

    video_packet_queue_.start();
    audio_packet_queue_.start();

    video_frame_queue_.init(&video_packet_queue_, 3, true);
    video_frame_queue_.start();

    audio_frame_queue_.init(&audio_packet_queue_, 9, false);
    audio_frame_queue_.start();

    vid_clk_.init(&video_packet_queue_.serial_ref());
    aud_clk_.init(&audio_packet_queue_.serial_ref());
    static std::atomic<int> ext_gen{0};
    ext_clk_.init(&ext_gen);

    demux_thread_ = std::thread(&video_decoder::demux_thread_func, this);
    return true;
}

void video_decoder::stop()
{
    abort_request_ = true;

    video_packet_queue_.abort();
    audio_packet_queue_.abort();
    video_frame_queue_.abort();
    audio_frame_queue_.abort();

    notify_packet_consumed();

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

void video_decoder::seek(double pos)
{
    seek_pos_ = pos;
    seek_req_ = true;
    notify_packet_consumed();
}

double video_decoder::get_duration() const
{
    if (fmt_ctx_ != nullptr && fmt_ctx_->duration != AV_NOPTS_VALUE)
    {
        return static_cast<double>(fmt_ctx_->duration) / AV_TIME_BASE;
    }
    return 0.0;
}

double video_decoder::get_frame_rate() const
{
    if (video_stream_ != nullptr)
    {
        return av_q2d(video_stream_->avg_frame_rate);
    }
    return 0.0;
}

void video_decoder::notify_packet_consumed()
{
    std::unique_lock<std::mutex> lock(continue_read_mutex_);
    continue_read_cv_.notify_one();
}

void video_decoder::demux_thread_func()
{
    AVPacket *pkt = av_packet_alloc();
    DEFER(av_packet_free(&pkt));

    fmt_ctx_ = avformat_alloc_context();
    if (fmt_ctx_ == nullptr)
    {
        LOG_ERROR("Failed to alloc avformat context");
        return;
    }

    fmt_ctx_->interrupt_callback.callback = decode_interrupt_cb;
    fmt_ctx_->interrupt_callback.opaque = this;

    if (avformat_open_input(&fmt_ctx_, file_.toStdString().c_str(), nullptr, nullptr) != 0)
    {
        LOG_ERROR("Failed to open input file");
        return;
    }

    if (avformat_find_stream_info(fmt_ctx_, nullptr) < 0)
    {
        LOG_ERROR("Failed to find stream info");
        return;
    }

    if (init_video_decoder(fmt_ctx_))
    {
        video_thread_ = std::thread(&video_decoder::video_thread_func, this);
    }

    if (init_audio_decoder(fmt_ctx_))
    {
        audio_thread_ = std::thread(&video_decoder::audio_thread_func, this);
    }

    while (!abort_request_)
    {
        if (seek_req_)
        {
            auto seek_target = static_cast<int64_t>(seek_pos_ * AV_TIME_BASE);
            if (avformat_seek_file(fmt_ctx_, -1, INT64_MIN, seek_target, INT64_MAX, 0) < 0)
            {
                LOG_ERROR("Seek failed");
            }
            else
            {
                video_packet_queue_.flush();
                audio_packet_queue_.flush();
                if (video_index_ >= 0)
                {
                    video_packet_queue_.put(pkt);
                }
                if (audio_index_ >= 0)
                {
                    audio_packet_queue_.put(pkt);
                }
                ext_clk_.set(seek_pos_, 0);
            }
            seek_req_ = false;
        }

        bool video_full = (video_index_ != -1) && (video_packet_queue_.size() > 15 * 1024 * 1024 ||
                                                   (static_cast<double>(video_packet_queue_.duration()) * av_q2d(video_stream_->time_base) > 1.0));
        bool audio_full = (audio_index_ != -1) && (audio_packet_queue_.size() > 5 * 1024 * 1024 ||
                                                   (static_cast<double>(audio_packet_queue_.duration()) * av_q2d(audio_stream_->time_base) > 1.0));

        if ((video_full || audio_full) && !abort_request_)
        {
            std::unique_lock<std::mutex> lock(continue_read_mutex_);
            continue_read_cv_.wait_for(lock, std::chrono::milliseconds(10));
            continue;
        }

        int ret = av_read_frame(fmt_ctx_, pkt);
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                std::unique_lock<std::mutex> lock(continue_read_mutex_);
                continue_read_cv_.wait_for(lock, std::chrono::milliseconds(100));
                continue;
            }
            if (fmt_ctx_->pb != nullptr && fmt_ctx_->pb->error != 0)
            {
                break;
            }
            continue;
        }

        if (pkt->stream_index == video_index_)
        {
            video_packet_queue_.put(pkt);
        }
        else if (pkt->stream_index == audio_index_)
        {
            audio_packet_queue_.put(pkt);
        }
        av_packet_unref(pkt);
    }
}

void video_decoder::video_thread_func()
{
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *sw_frame = av_frame_alloc();
    DEFER(av_packet_free(&pkt));
    DEFER(av_frame_free(&frame));
    DEFER(av_frame_free(&sw_frame));

    while (!abort_request_)
    {
        int serial;
        if (video_packet_queue_.get(pkt, &serial, true) < 0)
        {
            break;
        }

        notify_packet_consumed();

        if (serial != video_ctx_serial_)
        {
            avcodec_flush_buffers(video_ctx_);
            video_ctx_serial_ = serial;
        }

        if (avcodec_send_packet(video_ctx_, pkt) < 0)
        {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);

        while (true)
        {
            int ret = avcodec_receive_frame(video_ctx_, frame);
            if (ret < 0)
            {
                break;
            }

            AVFrame *final_frame = frame;
            bool needs_unref_sw = false;

            if (frame->format == hw_pix_fmt_)
            {
                if (av_hwframe_transfer_data(sw_frame, frame, 0) >= 0)
                {
                    av_frame_copy_props(sw_frame, frame);
                    final_frame = sw_frame;
                    needs_unref_sw = true;
                }
                else
                {
                    av_frame_unref(frame);
                    continue;
                }
            }

            Frame *vp = video_frame_queue_.peek_writable();
            if (vp == nullptr)
            {
                if (needs_unref_sw)
                {
                    av_frame_unref(sw_frame);
                }
                av_frame_unref(frame);
                break;
            }

            vp->serial = serial;
            vp->pts = (final_frame->pts == AV_NOPTS_VALUE) ? NAN : static_cast<double>(final_frame->pts) * av_q2d(video_stream_->time_base);
            vp->duration = (final_frame->duration == 0) ? 0 : static_cast<double>(final_frame->duration) * av_q2d(video_stream_->time_base);
            vp->width = final_frame->width;
            vp->height = final_frame->height;
            vp->format = final_frame->format;

            av_frame_move_ref(vp->frame, final_frame);
            video_frame_queue_.push();

            if (needs_unref_sw)
            {
                av_frame_unref(sw_frame);
            }
            av_frame_unref(frame);
        }
    }
}

void video_decoder::audio_thread_func()
{
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *frame_out = av_frame_alloc();

    DEFER(av_packet_free(&pkt));
    DEFER(av_frame_free(&frame));
    DEFER(av_frame_free(&frame_out));

    while (!abort_request_)
    {
        int serial;
        if (audio_packet_queue_.get(pkt, &serial, true) < 0)
        {
            break;
        }

        notify_packet_consumed();

        if (serial != audio_ctx_serial_)
        {
            avcodec_flush_buffers(audio_ctx_);
            audio_ctx_serial_ = serial;
        }

        if (avcodec_send_packet(audio_ctx_, pkt) < 0)
        {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);

        while (avcodec_receive_frame(audio_ctx_, frame) == 0)
        {
            AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;
            bool params_changed = (frame->sample_rate != last_audio_params_.sample_rate) ||
                                  (frame->ch_layout.nb_channels != last_audio_params_.channels) || (frame->format != last_audio_params_.format);

            if (params_changed || swr_ctx_ == nullptr)
            {
                if (swr_ctx_ != nullptr)
                {
                    swr_free(&swr_ctx_);
                }
                AVChannelLayout in_layout = frame->ch_layout;
                if (AV_CHANNEL_ORDER_UNSPEC == in_layout.order)
                {
                    av_channel_layout_default(&in_layout, frame->ch_layout.nb_channels);
                }

                swr_alloc_set_opts2(&swr_ctx_,
                                    &out_layout,
                                    AV_SAMPLE_FMT_S16,
                                    44100,
                                    &in_layout,
                                    static_cast<enum AVSampleFormat>(frame->format),
                                    frame->sample_rate,
                                    0,
                                    nullptr);
                swr_init(swr_ctx_);
                last_audio_params_.sample_rate = frame->sample_rate;
                last_audio_params_.channels = frame->ch_layout.nb_channels;
                last_audio_params_.format = frame->format;
            }

            int out_samples = static_cast<int>(
                av_rescale_rnd(swr_get_delay(swr_ctx_, frame->sample_rate) + frame->nb_samples, 44100, frame->sample_rate, AV_ROUND_UP));
            av_frame_unref(frame_out);
            frame_out->nb_samples = out_samples;
            frame_out->ch_layout = out_layout;
            frame_out->sample_rate = 44100;
            frame_out->format = AV_SAMPLE_FMT_S16;

            if (av_frame_get_buffer(frame_out, 0) >= 0)
            {
                int ret = swr_convert(swr_ctx_, frame_out->data, out_samples, (const uint8_t **)frame->data, frame->nb_samples);
                if (ret > 0)
                {
                    frame_out->nb_samples = ret;
                    Frame *af = audio_frame_queue_.peek_writable();
                    if (af != nullptr)
                    {
                        af->serial = serial;
                        af->pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : static_cast<double>(frame->pts) * av_q2d(audio_stream_->time_base);
                        af->duration = static_cast<double>(frame->nb_samples) / frame->sample_rate;
                        av_frame_move_ref(af->frame, frame_out);
                        audio_frame_queue_.push();
                    }
                }
            }
            av_frame_unref(frame);
        }
    }
}

bool video_decoder::init_hw_decoder(const AVCodec *codec)
{
    if (hw_device_ctx_ != nullptr)
    {
        av_buffer_unref(&hw_device_ctx_);
        hw_device_ctx_ = nullptr;
    }

    for (int i = 0;; i++)
    {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (config == nullptr)
        {
            break;
        }

        if (((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0) && config->device_type != AV_HWDEVICE_TYPE_NONE)
        {
            if (av_hwdevice_ctx_create(&hw_device_ctx_, config->device_type, nullptr, nullptr, 0) < 0)
            {
                continue;
            }
            hw_pix_fmt_ = config->pix_fmt;
            return true;
        }
    }
    return false;
}

bool video_decoder::open_codec_context(const AVCodec *codec, AVCodecParameters *par, bool try_hw)
{
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    if (ctx == nullptr)
    {
        return false;
    }
    if (avcodec_parameters_to_context(ctx, par) < 0)
    {
        avcodec_free_context(&ctx);
        return false;
    }

    ctx->opaque = this;
    if (try_hw)
    {
        ctx->get_format = get_hw_format;
        if (hw_device_ctx_ != nullptr)
        {
            ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
        }
    }
    else
    {
        ctx->thread_count = 0;
        if ((codec->capabilities & AV_CODEC_CAP_FRAME_THREADS) != 0)
        {
            ctx->thread_type = FF_THREAD_FRAME;
        }
        else if ((codec->capabilities & AV_CODEC_CAP_SLICE_THREADS) != 0)
        {
            ctx->thread_type = FF_THREAD_SLICE;
        }
    }

    if (avcodec_open2(ctx, codec, nullptr) < 0)
    {
        avcodec_free_context(&ctx);
        return false;
    }

    if (ctx->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        video_ctx_ = ctx;
    }
    else
    {
        audio_ctx_ = ctx;
    }
    return true;
}

bool video_decoder::init_video_decoder(AVFormatContext *fmt_ctx)
{
    video_index_ = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_index_ < 0)
    {
        return false;
    }

    video_stream_ = fmt_ctx->streams[video_index_];
    const AVCodec *codec = avcodec_find_decoder(video_stream_->codecpar->codec_id);
    if (codec == nullptr)
    {
        return false;
    }

    bool hw_success = false;
    if (init_hw_decoder(codec))
    {
        if (open_codec_context(codec, video_stream_->codecpar, true))
        {
            hw_success = true;
            is_hw_decoding_ = true;
        }
    }

    if (!hw_success)
    {
        if (hw_device_ctx_ != nullptr)
        {
            av_buffer_unref(&hw_device_ctx_);
        }
        hw_pix_fmt_ = AV_PIX_FMT_NONE;
        if (open_codec_context(codec, video_stream_->codecpar, false))
        {
            is_hw_decoding_ = false;
        }
        else
        {
            return false;
        }
    }
    return true;
}

bool video_decoder::init_audio_decoder(AVFormatContext *fmt_ctx)
{
    audio_index_ = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_index_ < 0)
    {
        return false;
    }

    audio_stream_ = fmt_ctx->streams[audio_index_];
    const AVCodec *codec = avcodec_find_decoder(audio_stream_->codecpar->codec_id);
    if (codec == nullptr)
    {
        return false;
    }

    if (!open_codec_context(codec, audio_stream_->codecpar, false))
    {
        return false;
    }

    return audio_out_->start(44100, 2, [this](uint8_t *stream, int len) { this->audio_callback_impl(stream, len); });
}

void video_decoder::audio_callback_impl(uint8_t *stream, int len)
{
    int len1;
    int audio_size;
    double audio_callback_time = static_cast<double>(av_gettime_relative()) / 1000000.0;

    while (len > 0)
    {
        if (abort_request_)
        {
            std::memset(stream, 0, static_cast<size_t>(len));
            return;
        }

        Frame *af = audio_frame_queue_.peek_readable();
        int current_frame_size = (af != nullptr) ? (af->frame->nb_samples * 4) : 0;

        if (af == nullptr || audio_buf_index_ >= current_frame_size)
        {
            if (af != nullptr)
            {
                if (af->serial != audio_packet_queue_.serial())
                {
                    audio_frame_queue_.next();
                    audio_buf_index_ = 0;
                    continue;
                }

                audio_current_pts_ = af->pts + af->duration;
                audio_frame_queue_.next();
                audio_buf_index_ = 0;
            }

            af = audio_frame_queue_.peek_readable();
            if (af == nullptr)
            {
                std::memset(stream, 0, static_cast<size_t>(len));
                return;
            }
        }

        audio_size = af->frame->nb_samples * 4;
        len1 = audio_size - audio_buf_index_;
        len1 = std::min(len1, len);

        std::memcpy(stream, af->frame->data[0] + audio_buf_index_, static_cast<size_t>(len1));

        len -= len1;
        stream += len1;
        audio_buf_index_ += len1;

        if (!std::isnan(af->pts))
        {
            double pts_in_frame = af->pts + (static_cast<double>(audio_buf_index_) / (4 * 44100.0));
            aud_clk_.set_at(pts_in_frame, af->serial, audio_callback_time);
        }
    }
}

double video_decoder::get_master_clock() { return aud_clk_.get(); }

void video_decoder::free_resources()
{
    if (swr_ctx_ != nullptr)
    {
        swr_free(&swr_ctx_);
    }
    if (video_ctx_ != nullptr)
    {
        avcodec_free_context(&video_ctx_);
    }
    if (audio_ctx_ != nullptr)
    {
        avcodec_free_context(&audio_ctx_);
    }
    if (fmt_ctx_ != nullptr)
    {
        avformat_close_input(&fmt_ctx_);
    }
    if (hw_device_ctx_ != nullptr)
    {
        av_buffer_unref(&hw_device_ctx_);
    }
}
