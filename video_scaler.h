#ifndef VIDEO_SCALER_H
#define VIDEO_SCALER_H

#include "media_objects.h"

extern "C"
{
#include <libswscale/swscale.h>
}

class video_scaler
{
   public:
    video_scaler() = default;
    ~video_scaler();

   public:
    bool convert(const AVFrame *src, AVFrame *dst);

   private:
    int src_w_ = 0;
    int src_h_ = 0;
    int dst_w_ = 0;
    int dst_h_ = 0;
    int src_fmt_ = -1;
    int dst_fmt_ = -1;
    SwsContext *sws_ctx_ = nullptr;
};

#endif
