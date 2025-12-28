#include "sdl_audio_backend.h"
#include "log.h"

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

    current_frame_ = nullptr;
    current_frame_offset_ = 0;
    current_frame_size_ = 0;

    last_serial_ = -1;

    if (SDL_Init(SDL_INIT_AUDIO) != 0)
    {
        LOG_ERROR("sdl init audio failed");
        return false;
    }

    SDL_AudioSpec wanted_spec = {0};
    wanted_spec.freq = 44100;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = 2;
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

    audio_buf_size_ = 192000;
    audio_buf_ = static_cast<uint8_t *>(av_malloc(audio_buf_size_));

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
    if (audio_dev_ != 0)
    {
        SDL_CloseAudioDevice(audio_dev_);
        audio_dev_ = 0;
    }

    if (audio_buf_ != nullptr)
    {
        av_free(audio_buf_);
        audio_buf_ = nullptr;
    }
}

void sdl_audio_backend::audio_callback_static(void *userdata, Uint8 *stream, int len)
{
    auto *backend = static_cast<sdl_audio_backend *>(userdata);
    backend->audio_callback(stream, len);
}

void sdl_audio_backend::audio_callback(Uint8 *stream, int len)
{
    SDL_memset(stream, 0, static_cast<size_t>(len));

    while (len > 0)
    {
        if (current_frame_ == nullptr || current_frame_offset_ >= current_frame_size_)
        {
            std::shared_ptr<media_frame> pkt_frame;

            if (frame_queue_->empty())
            {
                return;
            }
            if (!frame_queue_->pop(pkt_frame))
            {
                return;
            }
            if (pkt_frame == nullptr)
            {
                return;
            }

            if (!pkt_frame->flush() && pkt_frame->serial() != packet_queue_->serial())
            {
                continue;
            }

            if (pkt_frame->flush())
            {
                LOG_INFO("audio callback received flush");
                current_frame_ = nullptr;
                current_frame_offset_ = 0;
                current_frame_size_ = 0;
                continue;
            }

            current_frame_ = pkt_frame;
            current_frame_offset_ = 0;

            const double pts = static_cast<double>(current_frame_->raw()->pts) * av_q2d(time_base_);

            if (clock_ != nullptr)
            {
                if (current_frame_->serial() != last_serial_)
                {
                    LOG_INFO("audio detected seek serial changed {} -> {} resetting clock to {:.3f}", last_serial_, current_frame_->serial(), pts);
                    last_serial_ = current_frame_->serial();
                    clock_->set(pts, current_frame_->serial());
                }
                else
                {
                    LOG_TRACE("audio pts {:.3f} raw_pts {} updating clock", pts, current_frame_->raw()->pts);
                    clock_->set(pts, current_frame_->serial());
                }
            }

            AVChannelLayout *src_layout = &current_frame_->raw()->ch_layout;
            AVChannelLayout tgt_layout;
            av_channel_layout_default(&tgt_layout, 2);

            bool init_ret = resampler_.init(&tgt_layout,
                                            44100,
                                            AV_SAMPLE_FMT_S16,
                                            src_layout,
                                            current_frame_->raw()->sample_rate,
                                            static_cast<AVSampleFormat>(current_frame_->raw()->format));
            av_channel_layout_uninit(&tgt_layout);

            if (!init_ret)
            {
                LOG_ERROR("audio resampler init failed");
                current_frame_ = nullptr;
                current_frame_size_ = 0;
                continue;
            }

            const int out_samples =
                static_cast<int>(av_rescale_rnd(current_frame_->raw()->nb_samples, 44100, current_frame_->raw()->sample_rate, AV_ROUND_UP));

            int required_bytes = out_samples * 2 * 2;
            if (required_bytes > audio_buf_size_)
            {
                LOG_WARN("sdl audio buffer resize from {} to {}", audio_buf_size_, required_bytes * 2);
                if (audio_buf_ != nullptr)
                {
                    av_free(audio_buf_);
                }
                audio_buf_size_ = required_bytes * 2;
                audio_buf_ = static_cast<uint8_t *>(av_malloc(audio_buf_size_));
            }

            const int samples_converted = resampler_.convert(&audio_buf_, out_samples, current_frame_->raw());

            if (samples_converted <= 0)
            {
                LOG_ERROR("audio resampler convert failed or empty code {}", samples_converted);
                current_frame_ = nullptr;
                current_frame_size_ = 0;
                continue;
            }
            current_frame_size_ = samples_converted * 2 * 2;
        }

        int bytes_to_write = current_frame_size_ - current_frame_offset_;
        if (bytes_to_write > len)
        {
            bytes_to_write = len;
        }

        SDL_MixAudioFormat(stream, audio_buf_ + current_frame_offset_, AUDIO_S16SYS, static_cast<Uint32>(bytes_to_write), volume_.load());

        len -= bytes_to_write;
        stream += bytes_to_write;
        current_frame_offset_ += bytes_to_write;
    }
}
