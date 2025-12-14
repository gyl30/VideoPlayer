#ifndef VIDEO_FRAME_H
#define VIDEO_FRAME_H

#include <cstdint>

extern "C"
{
#include <libavutil/frame.h>
}

struct Frame
{
    AVFrame *frame;
    int serial;
    double pts;
    double duration;
    int64_t pos;
    int width;
    int height;
    bool uploaded;

    Frame()
    {
        frame = av_frame_alloc();
        serial = -1;
        pts = 0.0;
        duration = 0.0;
        pos = -1;
        width = 0;
        height = 0;
        uploaded = false;
    }

    ~Frame()
    {
        if (frame)
        {
            av_frame_free(&frame);
        }
    }
};

#endif
