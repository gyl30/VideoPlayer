#ifndef AV_CLOCK_H
#define AV_CLOCK_H

#include <cmath>

extern "C"
{
#include <libavutil/time.h>
}

class av_clock
{
   public:
    void init(int *queue_serial)
    {
        speed_ = 1.0;
        paused_ = false;
        queue_serial_ = queue_serial;
        set(NAN, -1);
    }

    void set(double pts, int serial)
    {
        double time = static_cast<double>(av_gettime_relative()) / 1000000.0;
        set_at(pts, serial, time);
    }

    void set_at(double pts, int serial, double time)
    {
        pts_ = pts;
        last_updated_ = time;
        pts_drift_ = pts_ - time;
        serial_ = serial;
    }

    [[nodiscard]] double get() const
    {
        if (*queue_serial_ != serial_)
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
        if (paused_ == paused)
        {
            return;
        }
        double time = static_cast<double>(av_gettime_relative()) / 1000000.0;
        if (paused)
        {
            pts_ = get();
        }
        else
        {
            pts_drift_ = pts_ - time;
            last_updated_ = time;
        }
        paused_ = paused;
    }

    [[nodiscard]] int serial() const { return serial_; }

   private:
    double pts_ = NAN;
    double pts_drift_ = 0;
    double last_updated_ = 0;
    double speed_ = 1.0;
    int serial_ = -1;
    bool paused_ = false;
    int *queue_serial_ = nullptr;
};

#endif
