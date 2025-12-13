#include <cstring>

#include "log.h"
#include "audio_output.h"

extern "C"
{
#include <libavutil/time.h>
}

audio_output::audio_output() { SDL_Init(SDL_INIT_AUDIO); }

audio_output::~audio_output()
{
    stop();
    SDL_Quit();
}

bool audio_output::start(int sample_rate, int channels)
{
    stop();

    sample_rate_ = sample_rate;
    channels_ = channels;
    bytes_per_sample_ = 2;

    SDL_AudioSpec wanted_spec;
    SDL_zero(wanted_spec);
    wanted_spec.freq = sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = static_cast<uint8_t>(channels);
    wanted_spec.samples = 1024;
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = this;

    SDL_AudioSpec obtained_spec;
    device_id_ = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &obtained_spec, 0);

    if (device_id_ == 0)
    {
        LOG_ERROR("SDL_OpenAudioDevice failed: {}", SDL_GetError());
        return false;
    }

    is_initialized_ = true;
    clock_.reset();
    SDL_PauseAudioDevice(device_id_, 0);
    return true;
}

void audio_output::stop()
{
    if (is_initialized_)
    {
        SDL_CloseAudioDevice(device_id_);
        device_id_ = 0;
        is_initialized_ = false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
    buffered_bytes_ = 0;
}

void audio_output::write(const uint8_t *data, size_t size)
{
    if (!is_initialized_ || size == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.insert(buffer_.end(), data, data + size);
    buffered_bytes_ += size;
}

void audio_output::sdl_audio_callback(void *userdata, uint8_t *stream, int len)
{
    auto *out = static_cast<audio_output *>(userdata);
    out->on_audio_callback(stream, len);
}

void audio_output::on_audio_callback(uint8_t *stream, int len)
{
    std::lock_guard<std::mutex> lock(mutex_);

    int to_read = len;
    int available = static_cast<int>(buffer_.size());

    if (available < to_read)
    {
        int i = 0;
        for (; i < available; ++i)
        {
            stream[i] = buffer_.front();
            buffer_.pop_front();
        }
        std::memset(stream + i, 0, static_cast<size_t>(to_read - i));
        buffered_bytes_ = 0;
    }
    else
    {
        for (int i = 0; i < to_read; ++i)
        {
            stream[i] = buffer_.front();
            buffer_.pop_front();
        }
        buffered_bytes_ -= static_cast<size_t>(to_read);
    }

    auto bytes_per_sec = static_cast<double>(sample_rate_ * channels_ * bytes_per_sample_);
    double buffer_duration = static_cast<double>(buffered_bytes_) / bytes_per_sec;
    double current_time = static_cast<double>(av_gettime_relative()) / 1000000.0;

    double pts = current_pts_ - buffer_duration;
    clock_.set(pts, current_time);
}

void audio_output::set_clock_at(double pts, double /*time*/) { current_pts_ = pts; }

double audio_output::get_current_time() const { return clock_.get(); }
