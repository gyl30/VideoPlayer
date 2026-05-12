#ifndef DECODER_H
#define DECODER_H

#include <string>
#include <atomic>
#include "safe_queue.h"
#include "media_objects.h"

extern "C"
{
#include <libavutil/hwcontext.h>
}

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
    static AVPixelFormat get_hw_format(AVCodecContext *ctx, const AVPixelFormat *pix_fmts);
    bool open_codec_context(bool try_hardware);
    bool reopen_software_decoder();
    void close_codec_context();

   private:
    std::string name_;
    AVCodecContext *codec_ctx_ = nullptr;
    AVCodecParameters *codec_par_ = nullptr;
    safe_queue<std::shared_ptr<media_frame>> *frame_queue_ = nullptr;
    safe_queue<std::shared_ptr<media_packet>> *packet_queue_ = nullptr;
    AVPixelFormat hw_pix_fmt_ = AV_PIX_FMT_NONE;
    AVHWDeviceType hw_device_type_ = AV_HWDEVICE_TYPE_NONE;
    bool video_decoder_ = false;
    bool using_hw_decode_ = false;
    std::atomic<bool> aborted_{false};
};

#endif
