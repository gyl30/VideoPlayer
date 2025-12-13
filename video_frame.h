#ifndef VIDEO_FRAME_H
#define VIDEO_FRAME_H

#include <vector>
#include <cstdint>
#include <memory>
#include <QMetaType>

struct video_frame
{
    int width = 0;
    int height = 0;

    std::vector<uint8_t> y_data;
    std::vector<uint8_t> u_data;
    std::vector<uint8_t> v_data;

    double pts = 0.0;
    double duration = 0.0;
    int y_line_size = 0;
    int u_line_size = 0;
    int v_line_size = 0;
};

using VideoFramePtr = std::shared_ptr<video_frame>;

Q_DECLARE_METATYPE(VideoFramePtr)

inline bool is_valid(const VideoFramePtr &frame) { return frame && frame->width > 0 && frame->height > 0 && !frame->y_data.empty(); }

#endif
