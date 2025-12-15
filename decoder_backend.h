#ifndef DECODER_BACKEND_H
#define DECODER_BACKEND_H

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/buffer.h>
#include <libavutil/pixdesc.h>
}

class decoder_backend
{
   public:
    virtual ~decoder_backend() = default;

    virtual bool init(AVStream *stream) = 0;
    virtual int send_packet(const AVPacket *pkt) = 0;
    virtual int receive_frame(AVFrame *frame) = 0;
    virtual void flush() = 0;
    virtual const char *name() const = 0;
    virtual AVCodecContext *get_context() const = 0;
    virtual enum AVPixelFormat get_pixel_format() const = 0;
};

class soft_decoder_backend : public decoder_backend
{
   public:
    soft_decoder_backend();
    ~soft_decoder_backend() override;

    bool init(AVStream *stream) override;
    int send_packet(const AVPacket *pkt) override;
    int receive_frame(AVFrame *frame) override;
    void flush() override;
    const char *name() const override;
    AVCodecContext *get_context() const override;
    enum AVPixelFormat get_pixel_format() const override;

   private:
    AVCodecContext *ctx_ = nullptr;
};

class hard_decoder_backend : public decoder_backend
{
   public:
    hard_decoder_backend();
    ~hard_decoder_backend() override;

    bool init(AVStream *stream) override;
    int send_packet(const AVPacket *pkt) override;
    int receive_frame(AVFrame *frame) override;
    void flush() override;
    const char *name() const override;
    AVCodecContext *get_context() const override;
    enum AVPixelFormat get_pixel_format() const override;

    enum AVPixelFormat get_hw_format_impl(const enum AVPixelFormat *pix_fmts) const;

   private:
    bool init_hw_device(const AVCodec *codec);

   private:
    AVCodecContext *ctx_ = nullptr;
    AVBufferRef *hw_device_ctx_ = nullptr;
    enum AVPixelFormat hw_pix_fmt_ = AV_PIX_FMT_NONE;
};

#endif
