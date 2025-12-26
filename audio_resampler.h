#ifndef AUDIO_RESAMPLER_H
#define AUDIO_RESAMPLER_H

extern "C"
{
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
}

class audio_resampler
{
   public:
    audio_resampler();
    ~audio_resampler();

   public:
    bool init(const AVChannelLayout *dst_layout,
              int dst_rate,
              AVSampleFormat dst_fmt,
              const AVChannelLayout *src_layout,
              int src_rate,
              AVSampleFormat src_fmt);
    int convert(uint8_t **out_buffer, int out_samples, const AVFrame *in_frame);

   private:
    SwrContext *swr_ctx_ = nullptr;
    AVChannelLayout in_ch_layout_;
    int in_rate_ = 0;
    AVSampleFormat in_fmt_ = AV_SAMPLE_FMT_NONE;
};

#endif
