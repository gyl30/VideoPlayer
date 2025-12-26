#ifndef SDL_AUDIO_BACKEND_H
#define SDL_AUDIO_BACKEND_H

#include <SDL.h>
#include <iostream>
#include <cstring>
#include <atomic>
#include "av_clock.h"
#include "safe_queue.h"
#include "media_objects.h"
#include "audio_resampler.h"

class sdl_audio_backend
{
   public:
    sdl_audio_backend() = default;

    ~sdl_audio_backend();

   public:
    bool init(safe_queue<std::shared_ptr<media_frame>> *frame_queue,
              safe_queue<std::shared_ptr<media_packet>> *packet_queue,
              AVRational tb,
              av_clock *clk);
    void pause(bool p) const;
    void set_volume(int percent);
    void close();

   private:
    static void audio_callback_static(void *userdata, Uint8 *stream, int len);
    void audio_callback(Uint8 *stream, int len);

   private:
    AVRational time_base_{0, 1};
    av_clock *clock_ = nullptr;
    audio_resampler resampler_;
    uint8_t *audio_buf_ = nullptr;
    uint32_t audio_buf_size_ = 0;
    int last_serial_ = -1;
    int current_frame_offset_ = 0;
    int current_frame_size_ = 0;
    SDL_AudioDeviceID audio_dev_ = 0;
    std::atomic<int> volume_{SDL_MIX_MAXVOLUME};
    std::shared_ptr<media_frame> current_frame_ = nullptr;
    safe_queue<std::shared_ptr<media_frame>> *frame_queue_ = nullptr;
    safe_queue<std::shared_ptr<media_packet>> *packet_queue_ = nullptr;
};

#endif
