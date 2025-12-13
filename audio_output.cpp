#include <SDL2/SDL.h>
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

    LOG_INFO("Audio Output Start: Rate={} Channels={}", sample_rate, channels);

    sample_rate_ = sample_rate;
    channels_ = channels;

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
        LOG_ERROR("SDL_OpenAudio failed: {}", SDL_GetError());
        return false;
    }

    SDL_PauseAudio(0);
    is_initialized_ = true;
    last_update_time_ = av_gettime_relative();
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
    total_bytes_ = 0;
    front_cursor_ = 0;
    audio_clock_ = 0.0;
}

void audio_output::write(const std::vector<uint8_t> &data, double pts)
{
    if (data.empty())
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.emplace_back(data, pts);
    total_bytes_ += data.size();

    static int log_counter = 0;
    if (++log_counter % 100 == 0)
    {
        double duration = static_cast<double>(total_bytes_) / (sample_rate_ * channels_ * bytes_per_sample_);
        LOG_DEBUG("Audio Buffer Written. Cached Duration: {:.3f}s", duration);
    }
}

double audio_output::get_buffer_duration()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (sample_rate_ == 0 || channels_ == 0)
    {
        return 0.0;
    }
    return static_cast<double>(total_bytes_) / (sample_rate_ * channels_ * bytes_per_sample_);
}

double audio_output::get_current_time()
{
    int64_t current_sys_time = av_gettime_relative();
    int64_t last_sys_time = last_update_time_.load();
    double pts = audio_clock_.load();

    double diff_sec = static_cast<double>(current_sys_time - last_sys_time) / 1000000.0;
    return pts + diff_sec;
}

void audio_output::sdl_audio_callback(void *userdata, uint8_t *stream, int len)
{
    auto *out = static_cast<audio_output *>(userdata);
    out->on_audio_callback(stream, len);
}

void audio_output::on_audio_callback(uint8_t *stream, int len)
{
    last_update_time_.store(av_gettime_relative());

    std::lock_guard<std::mutex> lock(mutex_);

    SDL_memset(stream, 0, static_cast<size_t>(len));

    double pts_of_this_chunk = 0.0;
    bool has_pts = false;

    if (total_bytes_ < static_cast<size_t>(len))
    {
        static int underrun_log_counter = 0;

        if (underrun_log_counter++ % 50 == 0)
        {
            LOG_WARN("[Audio Underrun] Need: {} bytes, Have: {} bytes. (This causes crackling)", len, total_bytes_.load());
        }
    }

    while (len > 0 && !buffer_.empty())
    {
        auto &chunk_pair = buffer_.front();
        std::vector<uint8_t> &chunk = chunk_pair.first;

        size_t available = chunk.size() - front_cursor_;

        if (!has_pts)
        {
            pts_of_this_chunk = chunk_pair.second;
            has_pts = true;
        }

        if (available > static_cast<size_t>(len))
        {
            SDL_MixAudio(stream, chunk.data() + front_cursor_, static_cast<Uint32>(len), SDL_MIX_MAXVOLUME);

            front_cursor_ += static_cast<size_t>(len);
            total_bytes_ -= static_cast<size_t>(len);
            audio_clock_ = pts_of_this_chunk;
            len = 0;
        }
        else
        {
            SDL_MixAudio(stream, chunk.data() + front_cursor_, static_cast<Uint32>(available), SDL_MIX_MAXVOLUME);

            len -= static_cast<int>(available);
            stream += available;
            total_bytes_ -= available;

            audio_clock_ = chunk_pair.second;
            buffer_.pop_front();
            front_cursor_ = 0;
        }
    }
}
