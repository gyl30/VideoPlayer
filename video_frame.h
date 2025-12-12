#ifndef VIDEO_FRAME_H
#define VIDEO_FRAME_H

#include <vector>
#include <cstdint>

struct video_frame
{
    int width = 0;
    int height = 0;

    std::vector<uint8_t> y_data;
    std::vector<uint8_t> u_data;
    std::vector<uint8_t> v_data;

    int y_line_size = 0;
    int u_line_size = 0;
    int v_line_size = 0;
};
inline bool is_valid(const video_frame &frame) { return frame.width > 0 && frame.height > 0 && !frame.y_data.empty(); }

#endif
