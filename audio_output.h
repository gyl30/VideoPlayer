#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include <SDL2/SDL.h>
#include <functional>
#include <atomic>

class audio_output
{
   public:
    using FillCallback = std::function<void(uint8_t *stream, int len)>;

    audio_output();
    ~audio_output();

    bool start(int sample_rate, int channels, FillCallback cb);
    void stop();

    int sample_rate() const { return sample_rate_; }
    int channels() const { return channels_; }

   private:
    static void sdl_audio_callback(void *userdata, uint8_t *stream, int len);

   private:
    SDL_AudioDeviceID device_id_ = 0;
    std::atomic<bool> is_initialized_{false};
    int sample_rate_ = 44100;
    int channels_ = 2;
    FillCallback callback_;
};

#endif
