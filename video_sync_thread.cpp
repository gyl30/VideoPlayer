#include "log.h"
#include "video_sync_thread.h"

video_sync_thread::video_sync_thread(safe_queue<std::shared_ptr<media_frame>> *frame_queue,
                                     safe_queue<std::shared_ptr<media_packet>> *packet_queue,
                                     AVRational tb,
                                     av_clock *clk,
                                     QObject *parent)
    : QThread(parent), clock_(clk), time_base_(tb), frame_queue_(frame_queue), packet_queue_(packet_queue)
{
    LOG_INFO("video sync thread created");
}

void video_sync_thread::stop()
{
    LOG_INFO("video sync thread stop requested");
    stop_ = true;
    this->requestInterruption();
}

void video_sync_thread::paused(bool p)
{
    LOG_INFO("video sync thread paused state {}", p);
    paused_.store(p);
}

void video_sync_thread::run()
{
    LOG_INFO("video sync thread run loop started");
    std::shared_ptr<media_frame> frame;
    auto render_frame = std::make_shared<media_frame>();

    while (!stop_ && !isInterruptionRequested())
    {
        if (paused_.load())
        {
            msleep(10);
            continue;
        }

        if (!frame_queue_->pop(frame))
        {
            LOG_INFO("video sync thread queue popped false exiting");
            break;
        }
        if (frame == nullptr)
        {
            LOG_INFO("video sync thread received null frame exiting");
            break;
        }

        if (!frame->flush() && frame->serial() != packet_queue_->serial())
        {
            continue;
        }

        if (frame->flush())
        {
            LOG_INFO("video sync thread received flush");
            continue;
        }

        const double pts = static_cast<double>(frame->raw()->pts) * av_q2d(time_base_);

        while (!stop_ && !isInterruptionRequested())
        {
            if (paused_.load())
            {
                msleep(10);
                continue;
            }

            if (clock_->serial() != frame->serial())
            {
                LOG_WARN("serial mismatch (clock: {}, frame: {}), forcing clock sync to video pts {:.3f}", clock_->serial(), frame->serial(), pts);
                clock_->set(pts, frame->serial());
                break;
            }

            const double master_clock = clock_->get();
            const double diff = pts - master_clock;

            LOG_TRACE("video pts {:.3f} raw pts {} master clock {:.3f} diff {:.3f}", pts, frame->raw()->pts, master_clock, diff);
            if (diff > 0.01)
            {
                auto sleep_ms = static_cast<uint64_t>(diff * 1000);
                if (sleep_ms > 50)
                {
                    sleep_ms = 50;
                }
                msleep(sleep_ms);
            }
            else
            {
                break;
            }
        }

        if (stop_ || isInterruptionRequested())
        {
            break;
        }

        const double final_diff = pts - clock_->get();
        if (final_diff < -0.2)
        {
            if (!frame_queue_->empty())
            {
                LOG_WARN("dropping video frame pts {:.3f} diff {:.3f}", pts, final_diff);
                continue;
            }
        }

        auto *raw_frame = render_frame->raw();

        if (raw_frame->width != frame->raw()->width || raw_frame->height != frame->raw()->height || raw_frame->format != AV_PIX_FMT_YUV420P)
        {
            av_frame_unref(raw_frame);
            raw_frame->format = AV_PIX_FMT_YUV420P;
            raw_frame->width = frame->raw()->width;
            raw_frame->height = frame->raw()->height;
            if (av_frame_get_buffer(raw_frame, 32) < 0)
            {
                LOG_ERROR("video sync av frame get buffer failed");
                continue;
            }
        }

        if (!scaler_.convert(frame->raw(), raw_frame))
        {
            LOG_ERROR("video sync scaler convert failed");
            continue;
        }

        auto frame_to_emit = std::make_shared<media_frame>();
        av_frame_move_ref(frame_to_emit->raw(), raw_frame);
        emit frame_ready(frame_to_emit);
    }
    LOG_INFO("video sync thread run loop finished");
}
