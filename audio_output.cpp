#include "audio_output.h"
#include <SDL2/SDL.h>

audio_output::audio_output() { SDL_Init(SDL_INIT_AUDIO); }

audio_output::~audio_output()
{
    stop();
    SDL_Quit();
}

bool audio_output::start(int sample_rate, int channels)
{
    stop();

    SDL_AudioSpec wanted_spec;
    SDL_zero(wanted_spec);
    wanted_spec.freq = sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = static_cast<uint8_t>(channels);
    wanted_spec.samples = 1024;
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = this;

    if (SDL_OpenAudio(&wanted_spec, nullptr) < 0)
    {
        return false;
    }

    SDL_PauseAudio(0);
    is_initialized_ = true;
    return true;
}

void audio_output::stop()
{
    if (is_initialized_)
    {
        SDL_CloseAudio();
        is_initialized_ = false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
}

void audio_output::write(const std::vector<uint8_t> &data)
{
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.push_back(data);
}

size_t audio_output::buffer_size()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size();
}

void audio_output::sdl_audio_callback(void *userdata, uint8_t *stream, int len)
{
    auto *out = static_cast<audio_output *>(userdata);
    out->on_audio_callback(stream, len);
}

void audio_output::on_audio_callback(uint8_t *stream, int len)
{
    std::lock_guard<std::mutex> lock(mutex_);
    SDL_memset(stream, 0, static_cast<size_t>(len));

    while (len > 0 && !buffer_.empty())
    {
        std::vector<uint8_t> &chunk = buffer_.front();
        int chunk_size = static_cast<int>(chunk.size());

        if (chunk_size > len)
        {
            SDL_MixAudio(stream, chunk.data(), static_cast<uint32_t>(len), SDL_MIX_MAXVOLUME);
            chunk.erase(chunk.begin(), chunk.begin() + len);
            len = 0;
        }
        else
        {
            SDL_MixAudio(stream, chunk.data(), static_cast<uint32_t>(chunk_size), SDL_MIX_MAXVOLUME);
            len -= chunk_size;
            stream += chunk_size;
            buffer_.pop_front();
        }
    }
}
