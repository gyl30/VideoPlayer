#include "video_scaler.h"
extern "C"
{
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

video_scaler::~video_scaler()
{
    if (sws_ctx_ != nullptr)
    {
        sws_freeContext(sws_ctx_);
    }
}

bool video_scaler::convert(const AVFrame *src, AVFrame *dst)
{
    if (src == nullptr || dst == nullptr)
    {
        return false;
    }

    if (sws_ctx_ == nullptr || src->width != src_w_ || src->height != src_h_ || src->format != src_fmt_ || dst->width != dst_w_ ||
        dst->height != dst_h_ || dst->format != dst_fmt_)
    {
        if (sws_ctx_ != nullptr)
        {
            sws_freeContext(sws_ctx_);
        }

        sws_ctx_ = sws_getContext(src->width,
                                  src->height,
                                  static_cast<AVPixelFormat>(src->format),
                                  dst->width,
                                  dst->height,
                                  static_cast<AVPixelFormat>(dst->format),
                                  SWS_BILINEAR,
                                  nullptr,
                                  nullptr,
                                  nullptr);

        if (sws_ctx_ == nullptr)
        {
            return false;
        }

        src_w_ = src->width;
        src_h_ = src->height;
        src_fmt_ = src->format;
        dst_w_ = dst->width;
        dst_h_ = dst->height;
        dst_fmt_ = dst->format;
    }

    sws_scale(sws_ctx_, src->data, src->linesize, 0, src->height, dst->data, dst->linesize);

    dst->pts = src->pts;

    return true;
}
