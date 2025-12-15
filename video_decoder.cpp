#include "video_decoder.h"
#include "log.h"
#include "scoped_exit.h"
#include "audio_output.h"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>

#include <algorithm>
}

int video_decoder::decode_interrupt_cb(void *ctx)
{
    auto *decoder = static_cast<video_decoder *>(ctx);
    return decoder->is_stopping() ? 1 : 0;
}

video_decoder::video_decoder(QObject *parent) : QObject(parent) { audio_out_ = std::make_unique<audio_output>(); }

video_decoder::~video_decoder() { stop(0); }

void video_decoder::open_async(const QString &file_path, int64_t op_id)
{
    stop(op_id);

    abort_request_ = false;

    file_ = file_path;
    LOG_INFO("op id {} video decoder start opening file", op_id);
    open_thread_ = std::thread(&video_decoder::open_thread_func, this, file_, op_id);
}

void video_decoder::open_thread_func(const QString &file, int64_t op_id)
{
    fmt_ctx_ = avformat_alloc_context();
    if (fmt_ctx_ == nullptr)
    {
        LOG_ERROR("op id {} failed to alloc avformat context", op_id);
        emit error_occurred(op_id, "failed to alloc avformat context");
        return;
    }

    fmt_ctx_->interrupt_callback.callback = decode_interrupt_cb;
    fmt_ctx_->interrupt_callback.opaque = this;

    int ret = avformat_open_input(&fmt_ctx_, file.toStdString().c_str(), nullptr, nullptr);
    if (ret != 0)
    {
        char errbuf[128] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf));
        LOG_ERROR("op id {} failed to open input file error {} {}", op_id, ret, errbuf);
        emit error_occurred(op_id, QString("failed to open input file: %1").arg(errbuf));
        return;
    }

    if (avformat_find_stream_info(fmt_ctx_, nullptr) < 0)
    {
        LOG_ERROR("op id {} failed to find stream info", op_id);
        emit error_occurred(op_id, "failed to find stream info");
        return;
    }

    if (fmt_ctx_->duration != AV_NOPTS_VALUE)
    {
        total_duration_ = static_cast<double>(fmt_ctx_->duration) / AV_TIME_BASE;
    }
    else
    {
        total_duration_ = 0.0;
    }

    if (!init_video_decoder(fmt_ctx_, op_id))
    {
        if (video_index_ >= 0)
        {
            LOG_ERROR("op id {} failed to init video decoder", op_id);
            emit error_occurred(op_id, "failed to init video decoder");
            return;
        }
    }

    init_audio_decoder(fmt_ctx_, op_id);

    LOG_INFO("op id {} media info loaded duration {:.2f}", op_id, total_duration_.load());
    emit media_info_loaded(op_id, total_duration_);
}

void video_decoder::start(int64_t op_id)
{
    LOG_INFO("op id {} video decoder receive start command", op_id);
    if (is_paused_)
    {
        abort_request_ = false;
        is_paused_ = false;

        if (!demux_thread_.joinable())
        {
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
            video_thread_ = std::thread(&video_decoder::video_thread_func, this);
            audio_thread_ = std::thread(&video_decoder::audio_thread_func, this);
            render_thread_ = std::thread(&video_decoder::render_thread_func, this);
        }
        else
        {
            vid_clk_.set_paused(false);
            aud_clk_.set_paused(false);
            ext_clk_.set_paused(false);
            notify_packet_consumed();
        }
    }
}

void video_decoder::stop(int64_t op_id)
{
    LOG_INFO("op id {} stopping decoder", op_id);
    abort_request_ = true;

    video_packet_queue_.abort();
    audio_packet_queue_.abort();
    video_frame_queue_.abort();
    audio_frame_queue_.abort();

    notify_packet_consumed();

    if (open_thread_.joinable())
    {
        open_thread_.join();
    }
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
    if (render_thread_.joinable())
    {
        render_thread_.join();
    }

    audio_out_->stop();
    free_resources();
    is_paused_ = true;
}

void video_decoder::seek_async(double pos, int64_t op_id)
{
    LOG_INFO("op id {} video decoder receive seek request to {:.2f}", op_id, pos);
    seek_pos_ = pos;
    seek_op_id_ = op_id;
    seek_req_ = true;
    is_paused_ = true;

    audio_buf_index_ = 0;

    notify_packet_consumed();
}

double video_decoder::get_duration() const { return total_duration_; }

double video_decoder::get_frame_rate() const
{
    if (video_stream_ != nullptr)
    {
        return av_q2d(video_stream_->avg_frame_rate);
    }
    return 0.0;
}

int video_decoder::current_hw_pix_fmt() const
{
    if (video_backend_)
    {
        return static_cast<int>(video_backend_->get_pixel_format());
    }
    return -1;
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

    while (!abort_request_)
    {
        if (seek_req_)
        {
            int64_t current_op = seek_op_id_;
            auto seek_target = static_cast<int64_t>(seek_pos_ * AV_TIME_BASE);

            LOG_INFO("op id {} executing internal seek", current_op);

            int ret = avformat_seek_file(fmt_ctx_, -1, INT64_MIN, seek_target, INT64_MAX, 0);

            if (ret < 0)
            {
                LOG_ERROR("op id {} seek failed", current_op);
                emit seek_finished(current_op, seek_pos_, false);
            }
            else
            {
                video_packet_queue_.flush();
                audio_packet_queue_.flush();

                ext_clk_.set(seek_pos_, 0);

                LOG_INFO("op id {} seek success notifying ui", current_op);
                emit seek_finished(current_op, seek_pos_, true);
            }
            seek_req_ = false;
        }

        if (is_paused_)
        {
            std::unique_lock<std::mutex> lock(continue_read_mutex_);
            continue_read_cv_.wait_for(lock, std::chrono::milliseconds(10));
            continue;
        }

        bool video_full = (video_index_ >= 0) && (video_packet_queue_.size() > 15 * 1024 * 1024 ||
                                                  (static_cast<double>(video_packet_queue_.duration()) * av_q2d(video_stream_->time_base) > 1.0));
        bool audio_full = (audio_index_ >= 0) && (audio_packet_queue_.size() > 5 * 1024 * 1024 ||
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
            video_backend_->flush();
            video_ctx_serial_ = serial;
        }

        if (video_backend_->send_packet(pkt) < 0)
        {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);

        while (true)
        {
            int ret = video_backend_->receive_frame(frame);
            if (ret < 0)
            {
                break;
            }

            AVFrame *final_frame = frame;
            bool frame_converted = false;

            if (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P && frame->format != AV_PIX_FMT_NV12 &&
                frame->format != AV_PIX_FMT_CUDA && frame->format != AV_PIX_FMT_VAAPI && frame->format != AV_PIX_FMT_D3D11)
            {
                if (img_convert_ctx_ == nullptr || av_image_get_buffer_size(AV_PIX_FMT_YUV420P, frame->width, frame->height, 1) < 0)
                {
                    if (img_convert_ctx_ != nullptr)
                    {
                        sws_freeContext(img_convert_ctx_);
                    }

                    img_convert_ctx_ = sws_getContext(frame->width,
                                                      frame->height,
                                                      static_cast<AVPixelFormat>(frame->format),
                                                      frame->width,
                                                      frame->height,
                                                      AV_PIX_FMT_YUV420P,
                                                      SWS_BICUBIC,
                                                      nullptr,
                                                      nullptr,
                                                      nullptr);
                }

                if (img_convert_ctx_ != nullptr)
                {
                    av_frame_unref(sw_frame);
                    sw_frame->format = AV_PIX_FMT_YUV420P;
                    sw_frame->width = frame->width;
                    sw_frame->height = frame->height;

                    if (av_frame_get_buffer(sw_frame, 32) >= 0)
                    {
                        sws_scale(img_convert_ctx_, frame->data, frame->linesize, 0, frame->height, sw_frame->data, sw_frame->linesize);

                        sw_frame->pts = frame->pts;
                        sw_frame->pkt_dts = frame->pkt_dts;
                        sw_frame->duration = frame->duration;
                        sw_frame->best_effort_timestamp = frame->best_effort_timestamp;

                        final_frame = sw_frame;
                        frame_converted = true;
                    }
                }
            }

            Frame *vp = video_frame_queue_.peek_writable();
            if (vp == nullptr)
            {
                av_frame_unref(frame);
                if (frame_converted)
                {
                    av_frame_unref(sw_frame);
                }
                break;
            }

            AVRational tb = video_stream_->time_base;
            vp->serial = serial;
            vp->pts = (final_frame->pts == AV_NOPTS_VALUE) ? NAN : static_cast<double>(final_frame->pts) * av_q2d(tb);
            vp->duration = (final_frame->duration == 0) ? 0 : static_cast<double>(final_frame->duration) * av_q2d(tb);
            vp->width = final_frame->width;
            vp->height = final_frame->height;
            vp->format = final_frame->format;

            av_frame_move_ref(vp->frame, final_frame);
            video_frame_queue_.push();

            if (frame_converted)
            {
                av_frame_unref(sw_frame);
            }
        }
    }
}

void video_decoder::render_thread_func()
{
    frame_timer_ = static_cast<double>(av_gettime_relative()) / 1000000.0;
    prev_frame_delay_ = 0.04;

    while (!abort_request_)
    {
        if (is_paused_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (video_frame_queue_.remaining() == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        Frame *lastvp = video_frame_queue_.peek_last();
        Frame *vp = video_frame_queue_.peek_readable();

        if (vp->serial != video_packet_queue_.serial())
        {
            video_frame_queue_.next();
            continue;
        }

        if (lastvp->serial != vp->serial)
        {
            frame_timer_ = static_cast<double>(av_gettime_relative()) / 1000000.0;
        }

        double duration = vp->duration;
        if (vp->serial == video_frame_queue_.peek_next()->serial)
        {
            double next_pts = video_frame_queue_.peek_next()->pts;
            if (!std::isnan(next_pts) && !std::isnan(vp->pts))
            {
                duration = next_pts - vp->pts;
            }
        }

        if (std::isnan(duration) || duration <= 0)
        {
            duration = prev_frame_delay_;
        }

        double delay = duration;
        double ref_clock = get_master_clock();
        double diff = vp->pts - ref_clock;

        double sync_threshold = (delay > 0.1) ? 0.1 : 0.04;

        if (!std::isnan(diff) && std::abs(diff) < 10.0)
        {
            if (diff <= -sync_threshold)
            {
                delay = std::max(0.0, delay + diff);
            }
            else if (diff >= sync_threshold && delay > 0.1)
            {
                delay = delay + diff;
            }
            else if (diff >= sync_threshold)
            {
                delay = 2 * delay;
            }
        }

        prev_frame_delay_ = delay;
        double time = static_cast<double>(av_gettime_relative()) / 1000000.0;

        if (time < frame_timer_ + delay)
        {
            av_usleep(static_cast<unsigned int>((frame_timer_ + delay - time) * 1000000.0));
            time = static_cast<double>(av_gettime_relative()) / 1000000.0;
        }

        frame_timer_ += delay;

        if (delay > 0 && time - frame_timer_ > 0.1)
        {
            frame_timer_ = time;
        }

        if (video_frame_queue_.remaining() > 1)
        {
            Frame *nextvp = video_frame_queue_.peek_next();
            double next_duration = 0.0;
            if (vp->serial == nextvp->serial)
            {
                next_duration = nextvp->pts - vp->pts;
                if (std::isnan(next_duration) || next_duration <= 0)
                {
                    next_duration = vp->duration;
                }
            }
            else
            {
                next_duration = vp->duration;
            }

            if (time > frame_timer_ + next_duration)
            {
                video_frame_queue_.next();
                continue;
            }
        }

        if (render_cb_)
        {
            render_cb_(vp);
        }
        video_frame_queue_.next();
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

bool video_decoder::open_codec_context(const AVCodec *codec, AVCodecParameters *par)
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
    ctx->thread_count = 0;
    if ((codec->capabilities & AV_CODEC_CAP_FRAME_THREADS) != 0)
    {
        ctx->thread_type = FF_THREAD_FRAME;
    }
    else if ((codec->capabilities & AV_CODEC_CAP_SLICE_THREADS) != 0)
    {
        ctx->thread_type = FF_THREAD_SLICE;
    }

    if (avcodec_open2(ctx, codec, nullptr) < 0)
    {
        avcodec_free_context(&ctx);
        return false;
    }

    if (ctx->codec_type == AVMEDIA_TYPE_AUDIO)
    {
        audio_ctx_ = ctx;
    }
    return true;
}

bool video_decoder::init_video_decoder(AVFormatContext *fmt_ctx, int64_t op_id)
{
    video_index_ = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_index_ < 0)
    {
        return false;
    }

    video_stream_ = fmt_ctx->streams[video_index_];

    LOG_INFO("op id {} attempting to initialize hardware decoder", op_id);
    video_backend_ = std::make_unique<hard_decoder_backend>();

    if (!video_backend_->init(video_stream_))
    {
        LOG_WARN("op id {} hardware decoder failed falling back to software decoder", op_id);
        video_backend_ = std::make_unique<soft_decoder_backend>();
        if (!video_backend_->init(video_stream_))
        {
            LOG_ERROR("op id {} software decoder failed too cannot play video", op_id);
            return false;
        }
    }

    LOG_INFO("op id {} video decoder backend ready {}", op_id, video_backend_->name());
    return true;
}

bool video_decoder::init_audio_decoder(AVFormatContext *fmt_ctx, int64_t /*op_id*/)
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

    if (!open_codec_context(codec, audio_stream_->codecpar))
    {
        return false;
    }

    return audio_out_->start(44100, 2, [this](uint8_t *stream, int len) { this->audio_callback_impl(stream, len); });
}

void video_decoder::audio_callback_impl(uint8_t *stream, int len)
{
    double audio_callback_time = static_cast<double>(av_gettime_relative()) / 1000000.0;

    while (len > 0)
    {
        if (abort_request_ || is_paused_)
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

        int audio_size = af->frame->nb_samples * 4;
        int len1 = audio_size - audio_buf_index_;
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
    video_backend_.reset();

    if (img_convert_ctx_ != nullptr)
    {
        sws_freeContext(img_convert_ctx_);
        img_convert_ctx_ = nullptr;
    }
    if (swr_ctx_ != nullptr)
    {
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
    }
    if (audio_ctx_ != nullptr)
    {
        avcodec_free_context(&audio_ctx_);
        audio_ctx_ = nullptr;
    }
    if (fmt_ctx_ != nullptr)
    {
        avformat_close_input(&fmt_ctx_);
        fmt_ctx_ = nullptr;
    }
}
