#ifndef VIDEO_FRAME_H
#define VIDEO_FRAME_H

#include <cstdint>
#include <memory>
#include <QMetaType>

extern "C"
{
#include <libavutil/frame.h>
}

struct video_frame
{
    std::shared_ptr<AVFrame> av_frame;
    double pts = 0.0;
    double duration = 0.0;

    static std::shared_ptr<video_frame> make(AVFrame *src, double pts, double duration)
    {
        auto vf = std::make_shared<video_frame>();
        vf->av_frame = std::shared_ptr<AVFrame>(av_frame_alloc(), [](AVFrame *f) { av_frame_free(&f); });

        if (src != nullptr)
        {
            av_frame_ref(vf->av_frame.get(), src);
        }

        vf->pts = pts;
        vf->duration = duration;
        return vf;
    }
};

using VideoFramePtr = std::shared_ptr<video_frame>;

Q_DECLARE_METATYPE(VideoFramePtr)

inline bool is_valid(const VideoFramePtr &frame) { return frame && frame->av_frame && frame->av_frame->width > 0 && frame->av_frame->height > 0; }

#endif
