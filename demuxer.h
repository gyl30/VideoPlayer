#ifndef DEMUXER_H
#define DEMUXER_H

#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include "safe_queue.h"
#include "media_objects.h"

class demuxer
{
   public:
    demuxer() = default;
    ~demuxer();
    demuxer(const demuxer &) = delete;
    demuxer &operator=(const demuxer &) = delete;

   public:
    bool open(const std::string &url, safe_queue<std::shared_ptr<media_packet>> *v_q, 
              safe_queue<std::shared_ptr<media_packet>> *a_q);
    void seek(double seconds);

    void stop();
    void run();

    void set_seek_cb(std::function<void(double)> cb);

   public:
    [[nodiscard]] int video_index() const;
    [[nodiscard]] int audio_index() const;
    [[nodiscard]] double duration() const;
    [[nodiscard]] AVRational time_base(int stream_index) const;
    [[nodiscard]] AVCodecParameters *codec_par(int stream_index) const;

   private:
    static int interrupt_cb(void *ctx);

   private:
    std::string url_;
    int video_index_ = -1;
    int audio_index_ = -1;
    AVFormatContext *fmt_ctx_ = nullptr;
    std::atomic<double> seek_req_{-1.0};

    std::atomic<bool> abort_{false};
    safe_queue<std::shared_ptr<media_packet>> *video_queue_ = nullptr;
    safe_queue<std::shared_ptr<media_packet>> *audio_queue_ = nullptr;

    std::function<void(double)> seek_cb_ = nullptr;
};

#endif
