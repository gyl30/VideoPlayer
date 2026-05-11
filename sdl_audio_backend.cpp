#include <algorithm>
#include <chrono>
#include "sdl_audio_backend.h"
#include "log.h"

namespace
{
double frame_pts_seconds(const AVFrame *frame, AVRational time_base)
{
    if (frame == nullptr || frame->pts == AV_NOPTS_VALUE)
    {
        return 0.0;
    }
    return static_cast<double>(frame->pts) * av_q2d(time_base);
}
}  // namespace

sdl_audio_backend::~sdl_audio_backend()
{
    LOG_INFO("sdl audio backend destroying");
    close();
}

bool sdl_audio_backend::init(safe_queue<std::shared_ptr<media_frame>> *frame_queue,
                             safe_queue<std::shared_ptr<media_packet>> *packet_queue,
                             AVRational tb,
                             av_clock *clk)
{
    LOG_INFO("sdl audio backend initializing");
    frame_queue_ = frame_queue;
    packet_queue_ = packet_queue;
    time_base_ = tb;
    clock_ = clk;
    queued_pcm_bytes_ = 0;
    stop_.store(false);

    clear_pcm_queue();

    if (SDL_Init(SDL_INIT_AUDIO) != 0)
    {
        LOG_ERROR("sdl init audio failed");
        return false;
    }

    SDL_AudioSpec wanted_spec = {0};
    wanted_spec.freq = k_output_sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = static_cast<Uint8>(k_output_channels);
    wanted_spec.silence = 0;
    wanted_spec.samples = 1024;
    wanted_spec.callback = audio_callback_static;
    wanted_spec.userdata = this;

    audio_dev_ = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, nullptr, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (audio_dev_ == 0)
    {
        LOG_ERROR("sdl open audio device failed");
        return false;
    }

    process_thread_ = std::thread([this]() { process_audio(); });

    SDL_PauseAudioDevice(audio_dev_, 0);
    LOG_INFO("sdl audio backend init success device id {}", audio_dev_);
    return true;
}

void sdl_audio_backend::pause(bool p) const
{
    if (audio_dev_ != 0)
    {
        SDL_PauseAudioDevice(audio_dev_, p ? 1 : 0);
    }
}

void sdl_audio_backend::set_volume(int percent)
{
    if (percent <= 0)
    {
        volume_.store(0);
        return;
    }
    if (percent > 100)
    {
        percent = 100;
    }

    const float factor = static_cast<float>(percent) / 100.0F;
    int vol = static_cast<int>(SDL_MIX_MAXVOLUME * factor * factor * factor);

    if (vol == 0 && percent > 0)
    {
        vol = 1;
    }
    volume_.store(vol);
}

void sdl_audio_backend::close()
{
    LOG_INFO("sdl audio backend closing");
    stop_.store(true);
    pcm_cond_.notify_all();

    if (frame_queue_ != nullptr)
    {
        frame_queue_->abort();
    }

    if (audio_dev_ != 0)
    {
        SDL_CloseAudioDevice(audio_dev_);
        audio_dev_ = 0;
    }

    if (process_thread_.joinable())
    {
        process_thread_.join();
    }

    clear_pcm_queue();
}

void sdl_audio_backend::audio_callback_static(void *userdata, Uint8 *stream, int len)
{
    auto *backend = static_cast<sdl_audio_backend *>(userdata);
    backend->audio_callback(stream, len);
}

void sdl_audio_backend::audio_callback(Uint8 *stream, int len)
{
    SDL_memset(stream, 0, static_cast<size_t>(len));

    std::lock_guard<std::mutex> lock(pcm_mutex_);

    while (len > 0 && !pcm_queue_.empty())
    {
        pcm_chunk &chunk = pcm_queue_.front();
        const int bytes_left = static_cast<int>(chunk.data.size() - chunk.offset);
        const int bytes_to_write = std::min(bytes_left, len);

        if (clock_ != nullptr)
        {
            const double played_frames = static_cast<double>(chunk.offset / static_cast<size_t>(k_output_bytes_per_frame));
            const double pts = chunk.pts + (played_frames / static_cast<double>(k_output_sample_rate));
            clock_->set(pts, chunk.serial);
        }

        SDL_MixAudioFormat(stream,
                           chunk.data.data() + chunk.offset,
                           AUDIO_S16SYS,
                           static_cast<Uint32>(bytes_to_write),
                           volume_.load());

        chunk.offset += static_cast<size_t>(bytes_to_write);
        stream += bytes_to_write;
        len -= bytes_to_write;

        if (chunk.offset >= chunk.data.size())
        {
            queued_pcm_bytes_ -= chunk.data.size();
            pcm_queue_.pop_front();
            pcm_cond_.notify_all();
        }
    }
}

void sdl_audio_backend::process_audio()
{
    LOG_INFO("sdl audio backend process loop started");

    while (!stop_.load())
    {
        {
            std::unique_lock<std::mutex> pcm_lock(pcm_mutex_);
            pcm_cond_.wait_for(
                pcm_lock,
                std::chrono::milliseconds(10),
                [this]() { return stop_.load() || queued_pcm_bytes_ < k_max_pcm_queue_bytes; });
        }

        if (stop_.load())
        {
            break;
        }

        std::shared_ptr<media_frame> frame;
        if (!frame_queue_->pop(frame))
        {
            if (stop_.load())
            {
                break;
            }
            continue;
        }

        if (frame == nullptr)
        {
            continue;
        }

        if (!frame->flush() && frame->serial() != packet_queue_->serial())
        {
            continue;
        }

        if (frame->flush())
        {
            LOG_INFO("audio process thread received flush");
            clear_pcm_queue();
            continue;
        }

#if LIBAVUTIL_VERSION_MAJOR >= 57
        audio_channel_layout src_layout = frame->raw()->ch_layout;
        audio_channel_layout tgt_layout;
        av_channel_layout_default(&tgt_layout, k_output_channels);
#else
        audio_channel_layout src_layout = frame->raw()->channel_layout;
        if (src_layout == 0)
        {
            src_layout = static_cast<uint64_t>(av_get_default_channel_layout(frame->raw()->channels));
        }
        const audio_channel_layout tgt_layout = AV_CH_LAYOUT_STEREO;
#endif

        const bool init_ok = resampler_.init(&tgt_layout,
                                             k_output_sample_rate,
                                             AV_SAMPLE_FMT_S16,
#if LIBAVUTIL_VERSION_MAJOR >= 57
                                             &src_layout,
#else
                                             &src_layout,
#endif
                                             frame->raw()->sample_rate,
                                             static_cast<AVSampleFormat>(frame->raw()->format));
#if LIBAVUTIL_VERSION_MAJOR >= 57
        av_channel_layout_uninit(&tgt_layout);
#endif
        if (!init_ok)
        {
            LOG_ERROR("audio process thread failed to init resampler");
            continue;
        }

        const int out_samples =
            static_cast<int>(av_rescale_rnd(frame->raw()->nb_samples, k_output_sample_rate, frame->raw()->sample_rate, AV_ROUND_UP));
        if (out_samples <= 0)
        {
            continue;
        }

        std::vector<uint8_t> pcm_buffer(static_cast<size_t>(out_samples * k_output_bytes_per_frame));
        uint8_t *buffer_ptr = pcm_buffer.data();
        const int samples_converted = resampler_.convert(&buffer_ptr, out_samples, frame->raw());
        if (samples_converted <= 0)
        {
            LOG_ERROR("audio process thread resampler convert failed or empty code {}", samples_converted);
            continue;
        }

        pcm_chunk chunk;
        chunk.data.resize(static_cast<size_t>(samples_converted * k_output_bytes_per_frame));
        std::copy_n(pcm_buffer.begin(), static_cast<long>(chunk.data.size()), chunk.data.begin());
        chunk.offset = 0;
        chunk.pts = frame_pts_seconds(frame->raw(), time_base_);
        chunk.serial = frame->serial();

        std::unique_lock<std::mutex> pcm_lock(pcm_mutex_);
        pcm_cond_.wait(pcm_lock, [this, &chunk]() { return stop_.load() || (queued_pcm_bytes_ + chunk.data.size()) <= k_max_pcm_queue_bytes; });
        if (stop_.load())
        {
            break;
        }

        queued_pcm_bytes_ += chunk.data.size();
        pcm_queue_.push_back(std::move(chunk));
        pcm_cond_.notify_all();
    }

    LOG_INFO("sdl audio backend process loop finished");
}

void sdl_audio_backend::clear_pcm_queue()
{
    std::lock_guard<std::mutex> lock(pcm_mutex_);
    pcm_queue_.clear();
    queued_pcm_bytes_ = 0;
    pcm_cond_.notify_all();
}
