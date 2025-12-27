#ifndef DECODER_H
#define DECODER_H

#include "safe_queue.h"
#include "media_objects.h"

class decoder
{
   public:
    decoder() = default;
    ~decoder();
    decoder(const decoder &) = delete;
    decoder &operator=(const decoder &) = delete;

   public:
    bool open(const AVCodecParameters *par,
              safe_queue<std::shared_ptr<media_packet>> *packet_queue,
              safe_queue<std::shared_ptr<media_frame>> *frame_queue,
              const std::string &name);

    void run();
    void stop();

   private:
    std::string name_;
    AVCodecContext *codec_ctx_ = nullptr;
    safe_queue<std::shared_ptr<media_frame>> *frame_queue_ = nullptr;
    safe_queue<std::shared_ptr<media_packet>> *packet_queue_ = nullptr;
    std::atomic<bool> aborted_{false};
};

#endif
