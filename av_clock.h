#ifndef AV_CLOCK_H
#define AV_CLOCK_H

#include <cmath>
#include <mutex>
#include <atomic>

extern "C"
{
#include <libavutil/time.h>
}

class av_clock
{
   public:
    void init(std::atomic<int> *queue_serial)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        speed_ = 1.0;
        paused_ = false;
        queue_serial_ = queue_serial;
        set_internal(NAN, -1, 0);
    }

    void set(double pts, int serial)
    {
        double time = static_cast<double>(av_gettime_relative()) / 1000000.0;
        set_at(pts, serial, time);
    }

    void set_at(double pts, int serial, double time)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        set_internal(pts, serial, time);
    }

    [[nodiscard]] double get() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_serial_ == nullptr || *queue_serial_ != serial_)
        {
            return NAN;
        }
        if (paused_)
        {
            return pts_;
        }
        double time = static_cast<double>(av_gettime_relative()) / 1000000.0;
        return pts_drift_ + time - ((time - last_updated_) * (1.0 - speed_));
    }

    void set_paused(bool paused)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (paused_ == paused)
        {
            return;
        }
        double time = static_cast<double>(av_gettime_relative()) / 1000000.0;
        if (paused)
        {
            pts_ = pts_drift_ + time - ((time - last_updated_) * (1.0 - speed_));
        }
        else
        {
            pts_drift_ = pts_ - time;
            last_updated_ = time;
        }
        paused_ = paused;
    }

    [[nodiscard]] int serial() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return serial_;
    }

   private:
    void set_internal(double pts, int serial, double time)
    {
        pts_ = pts;
        last_updated_ = time;
        pts_drift_ = pts_ - time;
        serial_ = serial;
    }

   private:
    double pts_ = NAN;
    double pts_drift_ = 0;
    double last_updated_ = 0;
    double speed_ = 1.0;
    int serial_ = -1;
    bool paused_ = false;
    std::atomic<int> *queue_serial_ = nullptr;
    mutable std::mutex mutex_;
};

#endif
