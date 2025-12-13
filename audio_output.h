#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include <vector>
#include <deque>
#include <mutex>
#include <cstdint>
#include <atomic>

class audio_output
{
   public:
    audio_output();
    ~audio_output();

   public:
    bool start(int sample_rate, int channels);
    void stop();
    void write(const std::vector<uint8_t> &data, double pts);
    double get_buffer_duration();
    double get_current_time();

   private:
    static void sdl_audio_callback(void *userdata, uint8_t *stream, int len);
    void on_audio_callback(uint8_t *stream, int len);

   private:
    std::deque<std::pair<std::vector<uint8_t>, double>> buffer_;
    std::mutex mutex_;
    bool is_initialized_ = false;
    std::atomic<size_t> total_bytes_{0};
    size_t front_cursor_ = 0;
    std::atomic<double> audio_clock_{0.0};
    std::atomic<int64_t> last_update_time_{0};
    int sample_rate_ = 44100;
    int channels_ = 2;
    int bytes_per_sample_ = 2;
};

#endif
