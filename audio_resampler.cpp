#include "log.h"
#include "audio_resampler.h"

extern "C"
{
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
}

audio_resampler::audio_resampler()
{
    LOG_INFO("audio resampler constructed");
    av_channel_layout_default(&in_ch_layout_, 0);
}

audio_resampler::~audio_resampler()
{
    LOG_INFO("audio resampler destroying");
    if (swr_ctx_ != nullptr)
    {
        swr_free(&swr_ctx_);
    }
    av_channel_layout_uninit(&in_ch_layout_);
}

bool audio_resampler::init(const AVChannelLayout *tgt_ch_layout,
                           int tgt_rate,
                           AVSampleFormat tgt_fmt,
                           const AVChannelLayout *src_ch_layout,
                           int src_rate,
                           AVSampleFormat src_fmt)
{
    if (swr_ctx_ != nullptr && av_channel_layout_compare(&in_ch_layout_, src_ch_layout) == 0 && in_rate_ == src_rate && in_fmt_ == src_fmt)
    {
        return true;
    }

    LOG_INFO("audio resampler initializing or reconfiguring src rate {} src fmt {}", src_rate, av_get_sample_fmt_name(src_fmt));

    if (swr_ctx_ != nullptr)
    {
        LOG_INFO("audio resampler freeing old context");
        swr_free(&swr_ctx_);
    }

    const int ret = swr_alloc_set_opts2(&swr_ctx_, tgt_ch_layout, tgt_fmt, tgt_rate, src_ch_layout, src_fmt, src_rate, 0, nullptr);

    if (ret < 0 || swr_ctx_ == nullptr || swr_init(swr_ctx_) < 0)
    {
        LOG_ERROR("audio resampler swr init failed");
        return false;
    }

    av_channel_layout_uninit(&in_ch_layout_);
    av_channel_layout_copy(&in_ch_layout_, src_ch_layout);

    in_rate_ = src_rate;
    in_fmt_ = src_fmt;

    LOG_INFO("audio resampler init success");
    return true;
}

int audio_resampler::convert(uint8_t **out_buffer, int out_samples, const AVFrame *in_frame)
{
    if (swr_ctx_ == nullptr)
    {
        LOG_WARN("audio resampler convert called with null context");
        return 0;
    }

    return swr_convert(swr_ctx_, out_buffer, out_samples, const_cast<const uint8_t **>(in_frame->data), in_frame->nb_samples);
}
