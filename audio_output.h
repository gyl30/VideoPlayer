#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include <vector>
#include <deque>
#include <mutex>
#include <cstdint>

class audio_output
{
   public:
    audio_output();
    ~audio_output();

    bool start(int sample_rate, int channels);
    void stop();
    void write(const std::vector<uint8_t> &data);
    size_t buffer_size();

   private:
    static void sdl_audio_callback(void *userdata, uint8_t *stream, int len);
    void on_audio_callback(uint8_t *stream, int len);

    std::deque<std::vector<uint8_t>> buffer_;
    std::mutex mutex_;
    bool is_initialized_ = false;
};

#endif
