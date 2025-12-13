#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include <deque>
#include <mutex>
#include <atomic>
#include <SDL2/SDL.h>
#include "av_clock.h"

class audio_output
{
   public:
    audio_output();
    ~audio_output();

    bool start(int sample_rate, int channels);
    void stop();
    void write(const uint8_t *data, size_t size);

    [[nodiscard]] double get_current_time() const;
    void set_clock_at(double pts, double time);

   private:
    static void sdl_audio_callback(void *userdata, uint8_t *stream, int len);
    void on_audio_callback(uint8_t *stream, int len);

   private:
    SDL_AudioDeviceID device_id_ = 0;
    std::deque<uint8_t> buffer_;
    std::mutex mutex_;
    std::atomic<bool> is_initialized_{false};
    int sample_rate_ = 44100;
    int channels_ = 2;
    int bytes_per_sample_ = 2;
    av_clock clock_;
    double current_pts_ = 0.0;
    size_t buffered_bytes_ = 0;
};

#endif
