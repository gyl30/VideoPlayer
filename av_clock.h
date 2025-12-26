#ifndef AV_CLOCK_H
#define AV_CLOCK_H

#include <atomic>

class av_clock
{
   public:
    av_clock();

   public:
    void set(double p);
    [[nodiscard]] double get() const;
    void pause();
    void resume();

   private:
    void update_time();

   private:
    double pts_at_pause_ = 0.0;
    std::atomic<double> pts_{0.0};
    std::atomic<bool> paused_{false};
    std::atomic<double> last_updated_{0.0};
};

#endif
