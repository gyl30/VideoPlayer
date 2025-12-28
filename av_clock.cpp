#include "av_clock.h"
extern "C"
{
#include <libavutil/time.h>
}

av_clock::av_clock() { update_time(); }

void av_clock::set(double p, int serial)
{
    pts_.store(p);
    serial_.store(serial);
    update_time();
}

double av_clock::get() const
{
    if (paused_.load())
    {
        return pts_at_pause_;
    }
    auto now = static_cast<double>(av_gettime_relative());
    const double time_elapsed = (now / 1000000.0) - last_updated_.load();
    return pts_.load() + time_elapsed;
}

int av_clock::serial() const { return serial_.load(); }

void av_clock::pause()
{
    pts_at_pause_ = get();
    paused_.store(true);
}

void av_clock::resume()
{
    pts_.store(pts_at_pause_);
    update_time();
    paused_.store(false);
}

void av_clock::update_time()
{
    auto now = static_cast<double>(av_gettime_relative());
    last_updated_.store(now / 1000000.0);
}
