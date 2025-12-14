#ifndef VIDEO_FRAME_H
#define VIDEO_FRAME_H

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
    int format;
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
        format = -1;
        uploaded = false;
    }

    ~Frame()
    {
        if (frame != nullptr)
        {
            av_frame_free(&frame);
        }
    }

    Frame(const Frame &) = delete;
    Frame &operator=(const Frame &) = delete;

    Frame(Frame &&other) noexcept
    {
        frame = other.frame;
        other.frame = nullptr;
        serial = other.serial;
        pts = other.pts;
        duration = other.duration;
        pos = other.pos;
        width = other.width;
        height = other.height;
        format = other.format;
        uploaded = other.uploaded;
    }

    Frame &operator=(Frame &&other) noexcept
    {
        if (this != &other)
        {
            if (frame != nullptr)
            {
                av_frame_free(&frame);
            }
            frame = other.frame;
            other.frame = nullptr;
            serial = other.serial;
            pts = other.pts;
            duration = other.duration;
            pos = other.pos;
            width = other.width;
            height = other.height;
            format = other.format;
            uploaded = other.uploaded;
        }
        return *this;
    }
};

#endif
