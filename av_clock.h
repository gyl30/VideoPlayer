#ifndef AV_CLOCK_H
#define AV_CLOCK_H

#include <atomic>
#include <cmath>

extern "C"
{
#include <libavutil/time.h>
}

class av_clock
{
   public:
    void set(double pts, double time)
    {
        pts_ = pts;
        last_updated_ = time;
        pts_drift_ = pts_ - time;
    }

    [[nodiscard]] double get() const
    {
        if (std::isnan(pts_))
        {
            return NAN;
        }
        double time = static_cast<double>(av_gettime_relative()) / 1000000.0;
        return pts_drift_ + time;
    }

    void reset()
    {
        pts_ = NAN;
        pts_drift_ = 0;
        last_updated_ = 0;
    }

   private:
    std::atomic<double> pts_{NAN};
    std::atomic<double> pts_drift_{0};
    std::atomic<double> last_updated_{0};
};

#endif
