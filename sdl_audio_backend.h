#ifndef SDL_AUDIO_BACKEND_H
#define SDL_AUDIO_BACKEND_H

#include <SDL.h>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
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
    struct pcm_chunk
    {
        std::vector<uint8_t> data;
        size_t offset = 0;
        double pts = 0.0;
        int serial = -1;
    };

    static void audio_callback_static(void *userdata, Uint8 *stream, int len);
    void audio_callback(Uint8 *stream, int len);
    void process_audio();
    void clear_pcm_queue();

   private:
    static constexpr int k_output_sample_rate = 44100;
    static constexpr int k_output_channels = 2;
    static constexpr int k_output_bytes_per_frame = 4;
    static constexpr size_t k_max_pcm_queue_bytes = static_cast<size_t>(k_output_sample_rate * k_output_bytes_per_frame * 2);

    AVRational time_base_{0, 1};
    av_clock *clock_ = nullptr;
    audio_resampler resampler_;
    SDL_AudioDeviceID audio_dev_ = 0;
    std::thread process_thread_;
    std::atomic<bool> stop_{false};
    std::atomic<int> volume_{SDL_MIX_MAXVOLUME};
    std::mutex pcm_mutex_;
    std::condition_variable pcm_cond_;
    std::deque<pcm_chunk> pcm_queue_;
    size_t queued_pcm_bytes_ = 0;
    safe_queue<std::shared_ptr<media_frame>> *frame_queue_ = nullptr;
    safe_queue<std::shared_ptr<media_packet>> *packet_queue_ = nullptr;
};

#endif
