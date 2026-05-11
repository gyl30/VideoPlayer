#ifndef AUDIO_RESAMPLER_H
#define AUDIO_RESAMPLER_H

#include <cstdint>

extern "C"
{
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/version.h>
}

#if LIBAVUTIL_VERSION_MAJOR >= 57
using audio_channel_layout = AVChannelLayout;
#else
using audio_channel_layout = uint64_t;
#endif

class audio_resampler
{
   public:
    audio_resampler();
    ~audio_resampler();

   public:
    bool init(const audio_channel_layout *dst_layout,
              int dst_rate,
              AVSampleFormat dst_fmt,
              const audio_channel_layout *src_layout,
              int src_rate,
              AVSampleFormat src_fmt);
    int convert(uint8_t **out_buffer, int out_samples, const AVFrame *in_frame);

   private:
    SwrContext *swr_ctx_ = nullptr;
    audio_channel_layout in_ch_layout_{};
    int in_rate_ = 0;
    AVSampleFormat in_fmt_ = AV_SAMPLE_FMT_NONE;
};

#endif
