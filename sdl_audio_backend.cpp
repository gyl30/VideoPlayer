#include <algorithm>
#include <chrono>
#include <cstdio>
#include "sdl_audio_backend.h"
#include "log.h"

extern "C"
{
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/samplefmt.h>
}

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

double normalize_playback_rate(double rate)
{
    if (rate < 0.5)
    {
        return 0.5;
    }
    if (rate > 2.0)
    {
        return 2.0;
    }
    return rate;
}

int frame_channels_compat(const AVFrame *frame)
{
    if (frame == nullptr)
    {
        return 0;
    }

#if LIBAVUTIL_VERSION_MAJOR >= 57
    return frame->ch_layout.nb_channels;
#else
    return frame->channels;
#endif
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
    playback_rate_.store(1.0);
    config_generation_.store(0);
    filter_src_rate_ = 0;
    filter_src_fmt_ = AV_SAMPLE_FMT_NONE;
    filter_playback_rate_ = 1.0;
    filter_sink_time_base_ = AVRational{0, 1};
#if LIBAVUTIL_VERSION_MAJOR >= 57
    av_channel_layout_uninit(&filter_src_layout_);
    av_channel_layout_default(&filter_src_layout_, 0);
#else
    filter_src_layout_ = 0;
#endif

    clear_pcm_queue();
    destroy_filter_graph();

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

void sdl_audio_backend::set_playback_rate(double rate)
{
    const double normalized_rate = normalize_playback_rate(rate);
    const double current_rate = playback_rate_.load();
    if (std::abs(current_rate - normalized_rate) < 0.0001)
    {
        return;
    }

    LOG_INFO("sdl audio backend playback rate {} -> {}", current_rate, normalized_rate);
    playback_rate_.store(normalized_rate);
    config_generation_.fetch_add(1);
    trim_pcm_queue_for_rate_change();
    pcm_cond_.notify_all();
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
    destroy_filter_graph();
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
            if (std::abs(clock_->rate() - chunk.playback_rate) >= 0.0001)
            {
                clock_->set_rate(chunk.playback_rate);
            }
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

    uint64_t active_generation = config_generation_.load();
    double media_cursor = 0.0;
    bool media_cursor_valid = false;

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
            destroy_filter_graph();
            active_generation = config_generation_.load();
            media_cursor = 0.0;
            media_cursor_valid = false;
            continue;
        }

        const uint64_t target_generation = config_generation_.load();
        if (target_generation != active_generation)
        {
            active_generation = target_generation;
        }

        const double playback_rate = playback_rate_.load();
        if (!media_cursor_valid && frame->raw()->pts != AV_NOPTS_VALUE)
        {
            media_cursor = frame_pts_seconds(frame->raw(), time_base_);
            media_cursor_valid = true;
        }
        if (filter_graph_ == nullptr || !filter_matches_frame(frame->raw()))
        {
            destroy_filter_graph();
            if (!configure_filter_graph(frame->raw(), playback_rate))
            {
                LOG_ERROR("audio process thread failed to configure filter graph");
                continue;
            }
        }
        else if (std::abs(filter_playback_rate_ - playback_rate) >= 0.0001)
        {
            if (!update_filter_playback_rate(playback_rate))
            {
                destroy_filter_graph();
                if (!configure_filter_graph(frame->raw(), playback_rate))
                {
                    LOG_ERROR("audio process thread failed to reconfigure filter graph");
                    continue;
                }
            }
        }

        AVFrame *input_frame = av_frame_clone(frame->raw());
        if (input_frame == nullptr)
        {
            LOG_ERROR("audio process thread failed to clone input frame");
            continue;
        }
        if (input_frame->pts != AV_NOPTS_VALUE)
        {
            input_frame->pts = av_rescale_q(input_frame->pts, time_base_, k_filter_time_base);
        }

        const int add_ret = av_buffersrc_add_frame_flags(buffersrc_ctx_, input_frame, AV_BUFFERSRC_FLAG_KEEP_REF);
        av_frame_free(&input_frame);
        if (add_ret < 0)
        {
            LOG_ERROR("audio process thread failed to feed filter graph code {}", add_ret);
            continue;
        }

        while (!stop_.load())
        {
            AVFrame *filtered_frame = av_frame_alloc();
            if (filtered_frame == nullptr)
            {
                LOG_ERROR("audio process thread failed to allocate filtered frame");
                break;
            }

            const int sink_ret = av_buffersink_get_frame(buffersink_ctx_, filtered_frame);
            if (sink_ret == AVERROR(EAGAIN) || sink_ret == AVERROR_EOF)
            {
                av_frame_free(&filtered_frame);
                break;
            }
            if (sink_ret < 0)
            {
                LOG_ERROR("audio process thread failed to pull filtered frame code {}", sink_ret);
                av_frame_free(&filtered_frame);
                break;
            }

            const int buffer_size =
                av_samples_get_buffer_size(nullptr, k_output_channels, filtered_frame->nb_samples, AV_SAMPLE_FMT_S16, 1);
            if (buffer_size <= 0)
            {
                av_frame_free(&filtered_frame);
                continue;
            }

            const int total_frames = buffer_size / k_output_bytes_per_frame;
            double chunk_base_pts = media_cursor;
            if (!media_cursor_valid)
            {
                const AVRational output_time_base =
                    (filter_sink_time_base_.num > 0 && filter_sink_time_base_.den > 0) ? filter_sink_time_base_ : k_filter_time_base;
                chunk_base_pts = frame_pts_seconds(filtered_frame, output_time_base);
                media_cursor = chunk_base_pts;
                media_cursor_valid = true;
            }
            const uint8_t *chunk_src = filtered_frame->data[0];
            av_frame_free(&filtered_frame);

            const uint64_t pending_generation = config_generation_.load();
            if (pending_generation != active_generation)
            {
                active_generation = pending_generation;
                break;
            }

            int frames_offset = 0;
            while (!stop_.load() && frames_offset < total_frames)
            {
                const int frames_this_chunk = std::min(k_output_chunk_frames, total_frames - frames_offset);
                const size_t bytes_this_chunk = static_cast<size_t>(frames_this_chunk * k_output_bytes_per_frame);

                pcm_chunk chunk;
                chunk.data.resize(bytes_this_chunk);
                std::copy_n(chunk_src + static_cast<ptrdiff_t>(frames_offset * k_output_bytes_per_frame),
                            static_cast<ptrdiff_t>(bytes_this_chunk),
                            chunk.data.data());
                chunk.offset = 0;
                chunk.pts = chunk_base_pts;
                chunk.serial = frame->serial();
                chunk.playback_rate = playback_rate;

                const uint64_t inner_pending_generation = config_generation_.load();
                if (inner_pending_generation != active_generation)
                {
                    active_generation = inner_pending_generation;
                    break;
                }

                std::unique_lock<std::mutex> pcm_lock(pcm_mutex_);
                pcm_cond_.wait(
                    pcm_lock, [this, &chunk]() { return stop_.load() || (queued_pcm_bytes_ + chunk.data.size()) <= k_max_pcm_queue_bytes; });
                if (stop_.load())
                {
                    break;
                }

                queued_pcm_bytes_ += chunk.data.size();
                pcm_queue_.push_back(std::move(chunk));
                pcm_cond_.notify_all();

                chunk_base_pts += (static_cast<double>(frames_this_chunk) / static_cast<double>(k_output_sample_rate)) * playback_rate;
                media_cursor = chunk_base_pts;
                frames_offset += frames_this_chunk;
            }
        }
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

void sdl_audio_backend::trim_pcm_queue_for_rate_change()
{
    std::lock_guard<std::mutex> lock(pcm_mutex_);
    if (pcm_queue_.empty())
    {
        queued_pcm_bytes_ = 0;
        pcm_cond_.notify_all();
        return;
    }

    pcm_chunk current = std::move(pcm_queue_.front());
    pcm_queue_.clear();
    queued_pcm_bytes_ = 0;

    if (current.offset < current.data.size())
    {
        queued_pcm_bytes_ = current.data.size();
        pcm_queue_.push_back(std::move(current));
    }

    pcm_cond_.notify_all();
}

void sdl_audio_backend::destroy_filter_graph()
{
    buffersrc_ctx_ = nullptr;
    buffersink_ctx_ = nullptr;
    tempo_ctx_ = nullptr;
    filter_sink_time_base_ = AVRational{0, 1};

    if (filter_graph_ != nullptr)
    {
        avfilter_graph_free(&filter_graph_);
    }

    filter_src_rate_ = 0;
    filter_src_fmt_ = AV_SAMPLE_FMT_NONE;
    filter_playback_rate_ = 1.0;
#if LIBAVUTIL_VERSION_MAJOR >= 57
    av_channel_layout_uninit(&filter_src_layout_);
    av_channel_layout_default(&filter_src_layout_, 0);
#else
    filter_src_layout_ = 0;
#endif
}

bool sdl_audio_backend::configure_filter_graph(const AVFrame *frame, double playback_rate)
{
    if (frame == nullptr)
    {
        return false;
    }

    const AVFilter *abuffer = avfilter_get_by_name("abuffer");
    const AVFilter *atempo = avfilter_get_by_name("atempo");
    const AVFilter *aformat = avfilter_get_by_name("aformat");
    const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");
    if (abuffer == nullptr || atempo == nullptr || aformat == nullptr || abuffersink == nullptr)
    {
        LOG_ERROR("audio filter graph missing required filters");
        return false;
    }

    filter_graph_ = avfilter_graph_alloc();
    if (filter_graph_ == nullptr)
    {
        LOG_ERROR("audio filter graph alloc failed");
        return false;
    }

#if LIBAVUTIL_VERSION_MAJOR >= 57
    audio_channel_layout src_layout = frame->ch_layout;
    if (src_layout.nb_channels == 0)
    {
        av_channel_layout_default(&src_layout, frame_channels_compat(frame));
    }
    char layout_desc[128] = {0};
    if (av_channel_layout_describe(&src_layout, layout_desc, sizeof(layout_desc)) < 0)
    {
        LOG_ERROR("audio filter graph failed to describe source channel layout");
        av_channel_layout_uninit(&src_layout);
        return false;
    }
#else
    const audio_channel_layout src_layout =
        frame->channel_layout != 0 ? frame->channel_layout : static_cast<uint64_t>(av_get_default_channel_layout(frame_channels_compat(frame)));
    char layout_desc[64] = {0};
    std::snprintf(layout_desc, sizeof(layout_desc), "0x%llx", static_cast<long long>(src_layout));
#endif

    char buffer_args[256] = {0};
    std::snprintf(buffer_args,
                  sizeof(buffer_args),
                  "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                  k_filter_time_base.num,
                  k_filter_time_base.den,
                  frame->sample_rate,
                  av_get_sample_fmt_name(static_cast<AVSampleFormat>(frame->format)),
                  layout_desc);

    int ret = avfilter_graph_create_filter(&buffersrc_ctx_, abuffer, "in", buffer_args, nullptr, filter_graph_);
    if (ret < 0)
    {
        LOG_ERROR("audio filter graph failed to create abuffer code {}", ret);
#if LIBAVUTIL_VERSION_MAJOR >= 57
        av_channel_layout_uninit(&src_layout);
#endif
        return false;
    }

    AVFilterContext *tempo_ctx = nullptr;
    char tempo_args[64] = {0};
    std::snprintf(tempo_args, sizeof(tempo_args), "tempo=%.6f", playback_rate);
    ret = avfilter_graph_create_filter(&tempo_ctx, atempo, "tempo", tempo_args, nullptr, filter_graph_);
    if (ret < 0)
    {
        LOG_ERROR("audio filter graph failed to create atempo code {}", ret);
#if LIBAVUTIL_VERSION_MAJOR >= 57
        av_channel_layout_uninit(&src_layout);
#endif
        return false;
    }

    AVFilterContext *format_ctx = nullptr;
    ret = avfilter_graph_create_filter(&format_ctx,
                                       aformat,
                                       "format",
                                       "sample_fmts=s16:sample_rates=44100:channel_layouts=stereo",
                                       nullptr,
                                       filter_graph_);
    if (ret < 0)
    {
        LOG_ERROR("audio filter graph failed to create aformat code {}", ret);
#if LIBAVUTIL_VERSION_MAJOR >= 57
        av_channel_layout_uninit(&src_layout);
#endif
        return false;
    }

    ret = avfilter_graph_create_filter(&buffersink_ctx_, abuffersink, "out", nullptr, nullptr, filter_graph_);
    if (ret < 0)
    {
        LOG_ERROR("audio filter graph failed to create abuffersink code {}", ret);
#if LIBAVUTIL_VERSION_MAJOR >= 57
        av_channel_layout_uninit(&src_layout);
#endif
        return false;
    }

    ret = avfilter_link(buffersrc_ctx_, 0, tempo_ctx, 0);
    if (ret >= 0)
    {
        ret = avfilter_link(tempo_ctx, 0, format_ctx, 0);
    }
    if (ret >= 0)
    {
        ret = avfilter_link(format_ctx, 0, buffersink_ctx_, 0);
    }
    if (ret < 0)
    {
        LOG_ERROR("audio filter graph failed to link filters code {}", ret);
#if LIBAVUTIL_VERSION_MAJOR >= 57
        av_channel_layout_uninit(&src_layout);
#endif
        return false;
    }

    ret = avfilter_graph_config(filter_graph_, nullptr);
    if (ret < 0)
    {
        LOG_ERROR("audio filter graph failed to config code {}", ret);
#if LIBAVUTIL_VERSION_MAJOR >= 57
        av_channel_layout_uninit(&src_layout);
#endif
        return false;
    }

    tempo_ctx_ = tempo_ctx;
#if LIBAVUTIL_VERSION_MAJOR >= 57
    av_channel_layout_uninit(&filter_src_layout_);
    av_channel_layout_copy(&filter_src_layout_, &src_layout);
    av_channel_layout_uninit(&src_layout);
#else
    filter_src_layout_ = src_layout;
#endif
    filter_src_rate_ = frame->sample_rate;
    filter_src_fmt_ = static_cast<AVSampleFormat>(frame->format);
    filter_playback_rate_ = playback_rate;
    filter_sink_time_base_ = av_buffersink_get_time_base(buffersink_ctx_);
    LOG_INFO("audio filter graph configured playback rate {}", playback_rate);
    return true;
}

bool sdl_audio_backend::update_filter_playback_rate(double playback_rate)
{
    if (tempo_ctx_ == nullptr)
    {
        return false;
    }

    char tempo_arg[32] = {0};
    std::snprintf(tempo_arg, sizeof(tempo_arg), "%.6f", playback_rate);

    const int ret = avfilter_process_command(tempo_ctx_, "tempo", tempo_arg, nullptr, 0, 0);
    if (ret < 0)
    {
        LOG_ERROR("audio filter graph failed to update atempo code {}", ret);
        return false;
    }

    filter_playback_rate_ = playback_rate;
    LOG_INFO("audio filter graph updated playback rate {}", playback_rate);
    return true;
}

bool sdl_audio_backend::filter_matches_frame(const AVFrame *frame) const
{
    if (frame == nullptr)
    {
        return false;
    }

    if (filter_src_rate_ != frame->sample_rate || filter_src_fmt_ != static_cast<AVSampleFormat>(frame->format))
    {
        return false;
    }

#if LIBAVUTIL_VERSION_MAJOR >= 57
    audio_channel_layout frame_layout = frame->ch_layout;
    if (frame_layout.nb_channels == 0)
    {
        av_channel_layout_default(&frame_layout, frame_channels_compat(frame));
    }
    const bool same_layout = av_channel_layout_compare(&filter_src_layout_, &frame_layout) == 0;
    av_channel_layout_uninit(&frame_layout);
    return same_layout;
#else
    const audio_channel_layout frame_layout =
        frame->channel_layout != 0 ? frame->channel_layout : static_cast<uint64_t>(av_get_default_channel_layout(frame_channels_compat(frame)));
    return filter_src_layout_ == frame_layout;
#endif
}
