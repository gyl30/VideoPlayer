#include "audio_output.h"
#include "log.h"
#include <cstring>

audio_output::audio_output() { SDL_Init(SDL_INIT_AUDIO); }

audio_output::~audio_output()
{
    stop();
    SDL_Quit();
}

bool audio_output::start(int sample_rate, int channels, FillCallback cb)
{
    stop();
    callback_ = std::move(cb);

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

    sample_rate_ = obtained_spec.freq;
    channels_ = obtained_spec.channels;

    is_initialized_ = true;
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
}

void audio_output::sdl_audio_callback(void *userdata, uint8_t *stream, int len)
{
    auto *out = static_cast<audio_output *>(userdata);
    if (out->callback_)
    {
        out->callback_(stream, len);
    }
    else
    {
        std::memset(stream, 0, len);
    }
}
