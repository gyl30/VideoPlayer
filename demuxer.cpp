#include <thread>
#include "demuxer.h"
#include "log.h"

demuxer::~demuxer()
{
    LOG_INFO("demuxer destroying");

    stop();
    if (fmt_ctx_ != nullptr)
    {
        avformat_close_input(&fmt_ctx_);
    }
}

void demuxer::stop() { abort_.store(true); }

int demuxer::interrupt_cb(void *ctx)
{
    auto *self = static_cast<demuxer *>(ctx);
    if (self->abort_.load())
    {
        return 1;
    }
    return 0;
}

void demuxer::set_seek_cb(std::function<void(double)> cb) { seek_cb_ = std::move(cb); }

[[nodiscard]] AVCodecParameters *demuxer::codec_par(int stream_index) const
{
    if (stream_index < 0 || stream_index >= static_cast<int>(fmt_ctx_->nb_streams))
    {
        return nullptr;
    }
    return fmt_ctx_->streams[stream_index]->codecpar;
}

[[nodiscard]] AVRational demuxer::time_base(int stream_index) const
{
    if (stream_index < 0 || stream_index >= static_cast<int>(fmt_ctx_->nb_streams))
    {
        return AVRational{0, 1};
    }
    return fmt_ctx_->streams[stream_index]->time_base;
}

[[nodiscard]] int demuxer::video_index() const { return video_index_; }

[[nodiscard]] int demuxer::audio_index() const { return audio_index_; }

[[nodiscard]] double demuxer::duration() const
{
    if (fmt_ctx_ == nullptr || fmt_ctx_->duration == AV_NOPTS_VALUE)
    {
        return 0.0;
    }
    return static_cast<double>(fmt_ctx_->duration) / AV_TIME_BASE;
}

void demuxer::seek(double seconds)
{
    LOG_INFO("demuxer seek requested to {}", seconds);
    if (video_queue_ != nullptr)
    {
        video_queue_->add_serial();
    }
    if (audio_queue_ != nullptr)
    {
        audio_queue_->add_serial();
    }
    seek_req_.store(seconds);

    if (video_queue_ != nullptr)
    {
        video_queue_->abort();
    }
    if (audio_queue_ != nullptr)
    {
        audio_queue_->abort();
    }
}

bool demuxer::open(const std::string &url, safe_queue<std::shared_ptr<media_packet>> *v_q, safe_queue<std::shared_ptr<media_packet>> *a_q)
{
    LOG_INFO("demuxer opening url {}", url);
    url_ = url;
    video_queue_ = v_q;
    audio_queue_ = a_q;
    abort_.store(false);

    fmt_ctx_ = avformat_alloc_context();
    if (fmt_ctx_ == nullptr)
    {
        LOG_ERROR("demuxer avformat alloc context failed");
        return false;
    }

    fmt_ctx_->interrupt_callback.callback = interrupt_cb;
    fmt_ctx_->interrupt_callback.opaque = this;

    if (avformat_open_input(&fmt_ctx_, url_.c_str(), nullptr, nullptr) != 0)
    {
        LOG_ERROR("demuxer avformat open input failed for {}", url);
        return false;
    }
    if (avformat_find_stream_info(fmt_ctx_, nullptr) < 0)
    {
        LOG_ERROR("demuxer avformat find stream info failed");
        return false;
    }

    video_index_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audio_index_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    LOG_INFO("demuxer open success video index {} audio index {}", video_index_, audio_index_);
    return true;
}

void demuxer::run()
{
    if (fmt_ctx_ == nullptr)
    {
        LOG_WARN("demuxer run called with null context");
        return;
    }

    LOG_INFO("demuxer loop started");

    bool eof_reached = false;

    while (!abort_.load())
    {
        const double target = seek_req_.exchange(-1.0);
        if (target >= 0.0)
        {
            LOG_INFO("demuxer performing seek to {}", target);
            const auto seek_target = static_cast<int64_t>(target * AV_TIME_BASE);

            const int64_t seek_min = INT64_MIN;
            const int64_t seek_max = INT64_MAX;

            const int ret = avformat_seek_file(fmt_ctx_, -1, seek_min, seek_target, seek_max, AVSEEK_FLAG_BACKWARD);
            if (ret < 0)
            {
                LOG_ERROR("demuxer seek failed code {}", ret);
            }
            else
            {
                LOG_INFO("demuxer seek success clearing queues");

                if (video_queue_ != nullptr)
                {
                    video_queue_->reset();
                }
                if (audio_queue_ != nullptr)
                {
                    audio_queue_->reset();
                }

                if (video_queue_ != nullptr)
                {
                    video_queue_->clear();
                }
                if (audio_queue_ != nullptr)
                {
                    audio_queue_->clear();
                }

                if (video_queue_ != nullptr)
                {
                    auto pkt = media_packet::create_flush();
                    pkt->set_serial(video_queue_->serial());
                    video_queue_->push(pkt);
                }
                if (audio_queue_ != nullptr)
                {
                    auto pkt = media_packet::create_flush();
                    pkt->set_serial(audio_queue_->serial());
                    audio_queue_->push(pkt);
                }

                if (seek_cb_)
                {
                    seek_cb_(target);
                }

                eof_reached = false;
            }
        }

        if (eof_reached)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto pkt = std::make_shared<media_packet>();
        const int ret = av_read_frame(fmt_ctx_, pkt->raw());
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                LOG_INFO("demuxer reached end of file");
                eof_reached = true;

                if (video_queue_ != nullptr)
                {
                    video_queue_->push(nullptr);
                }
                if (audio_queue_ != nullptr)
                {
                    audio_queue_->push(nullptr);
                }

                continue;
            }

            if (abort_.load())
            {
                LOG_INFO("demuxer aborted during read frame");
                break;
            }
            LOG_ERROR("demuxer read frame failed code {}", ret);
            eof_reached = true;
            continue;
        }

        if (pkt->raw()->stream_index == video_index_ && video_queue_ != nullptr)
        {
            pkt->set_serial(video_queue_->serial());
            if (!video_queue_->push(pkt))
            {
                if (seek_req_.load() >= 0.0)
                {
                    continue;
                }
                LOG_INFO("demuxer video queue push failed");
                break;
            }
        }
        else if (pkt->raw()->stream_index == audio_index_ && audio_queue_ != nullptr)
        {
            pkt->set_serial(audio_queue_->serial());
            if (!audio_queue_->push(pkt))
            {
                if (seek_req_.load() >= 0.0)
                {
                    continue;
                }
                LOG_INFO("demuxer audio queue push failed");
                break;
            }
        }
    }

    LOG_INFO("demuxer loop ending");
}
