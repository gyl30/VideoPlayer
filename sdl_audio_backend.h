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
#include <cmath>
#include "av_clock.h"
#include "safe_queue.h"
#include "media_objects.h"
#include "audio_resampler.h"

extern "C"
{
#include <libavfilter/avfilter.h>
}

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
    void set_playback_rate(double rate);
    void set_volume(int percent);
    void close();

   private:
    struct pcm_chunk
    {
        std::vector<uint8_t> data;
        size_t offset = 0;
        double pts = 0.0;
        int serial = -1;
        double playback_rate = 1.0;
    };

    static void audio_callback_static(void *userdata, Uint8 *stream, int len);
    void audio_callback(Uint8 *stream, int len);
    void process_audio();
    void clear_pcm_queue();
    void trim_pcm_queue_for_rate_change();
    void destroy_filter_graph();
    bool configure_filter_graph(const AVFrame *frame, double playback_rate);
    bool filter_matches_frame(const AVFrame *frame) const;
    bool update_filter_playback_rate(double playback_rate);

   private:
    static constexpr int k_output_sample_rate = 44100;
    static constexpr int k_output_channels = 2;
    static constexpr int k_output_bytes_per_frame = 4;
    static constexpr int k_output_chunk_frames = 512;
    static constexpr size_t k_max_pcm_queue_bytes = static_cast<size_t>(k_output_sample_rate * k_output_bytes_per_frame * 2);
    static constexpr AVRational k_filter_time_base = {1, AV_TIME_BASE};

    AVRational time_base_{0, 1};
    av_clock *clock_ = nullptr;
    SDL_AudioDeviceID audio_dev_ = 0;
    std::thread process_thread_;
    std::atomic<bool> stop_{false};
    std::atomic<double> playback_rate_{1.0};
    std::atomic<uint64_t> config_generation_{0};
    std::atomic<int> volume_{SDL_MIX_MAXVOLUME};
    std::mutex pcm_mutex_;
    std::condition_variable pcm_cond_;
    std::deque<pcm_chunk> pcm_queue_;
    size_t queued_pcm_bytes_ = 0;
    AVFilterGraph *filter_graph_ = nullptr;
    AVFilterContext *buffersrc_ctx_ = nullptr;
    AVFilterContext *buffersink_ctx_ = nullptr;
    AVFilterContext *tempo_ctx_ = nullptr;
    AVRational filter_sink_time_base_{0, 1};
    audio_channel_layout filter_src_layout_{};
    int filter_src_rate_ = 0;
    AVSampleFormat filter_src_fmt_ = AV_SAMPLE_FMT_NONE;
    double filter_playback_rate_ = 1.0;
    safe_queue<std::shared_ptr<media_frame>> *frame_queue_ = nullptr;
    safe_queue<std::shared_ptr<media_packet>> *packet_queue_ = nullptr;
};

#endif
