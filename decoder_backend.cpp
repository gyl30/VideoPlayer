#include "log.h"
#include "decoder_backend.h"

soft_decoder_backend::soft_decoder_backend() {}

soft_decoder_backend::~soft_decoder_backend()
{
    if (ctx_ != nullptr)
    {
        avcodec_free_context(&ctx_);
    }
}

bool soft_decoder_backend::init(AVStream *stream)
{
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr)
    {
        LOG_ERROR("Codec not found");
        return false;
    }

    ctx_ = avcodec_alloc_context3(codec);
    if (ctx_ == nullptr)
    {
        return false;
    }

    if (avcodec_parameters_to_context(ctx_, stream->codecpar) < 0)
    {
        return false;
    }

    if ((codec->capabilities & AV_CODEC_CAP_FRAME_THREADS) != 0)
    {
        ctx_->thread_type = FF_THREAD_FRAME;
    }
    else if ((codec->capabilities & AV_CODEC_CAP_SLICE_THREADS) != 0)
    {
        ctx_->thread_type = FF_THREAD_SLICE;
    }

    if (avcodec_open2(ctx_, codec, nullptr) < 0)
    {
        LOG_ERROR("Could not open software codec");
        return false;
    }

    LOG_INFO("Software decoder initialized: {}", codec->name);
    return true;
}

int soft_decoder_backend::send_packet(const AVPacket *pkt)
{
    if (ctx_ == nullptr)
    {
        return -1;
    }
    return avcodec_send_packet(ctx_, pkt);
}

int soft_decoder_backend::receive_frame(AVFrame *frame)
{
    if (ctx_ == nullptr)
    {
        return -1;
    }
    return avcodec_receive_frame(ctx_, frame);
}

void soft_decoder_backend::flush()
{
    if (ctx_ != nullptr)
    {
        avcodec_flush_buffers(ctx_);
    }
}

const char *soft_decoder_backend::name() const { return "Software"; }

AVCodecContext *soft_decoder_backend::get_context() const { return ctx_; }

enum AVPixelFormat soft_decoder_backend::get_pixel_format() const { return AV_PIX_FMT_NONE; }

static enum AVPixelFormat get_hw_format_cb(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts)
{
    auto *backend = static_cast<hard_decoder_backend *>(ctx->opaque);
    return backend->get_hw_format_impl(pix_fmts);
}

hard_decoder_backend::hard_decoder_backend() {}

hard_decoder_backend::~hard_decoder_backend()
{
    if (ctx_ != nullptr)
    {
        avcodec_free_context(&ctx_);
    }
    if (hw_device_ctx_ != nullptr)
    {
        av_buffer_unref(&hw_device_ctx_);
    }
}

bool hard_decoder_backend::init(AVStream *stream)
{
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr)
    {
        return false;
    }

    if (!init_hw_device(codec))
    {
        return false;
    }

    ctx_ = avcodec_alloc_context3(codec);
    if (ctx_ == nullptr)
    {
        return false;
    }

    if (avcodec_parameters_to_context(ctx_, stream->codecpar) < 0)
    {
        return false;
    }

    ctx_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
    ctx_->get_format = get_hw_format_cb;
    ctx_->opaque = this;

    if (avcodec_open2(ctx_, codec, nullptr) < 0)
    {
        LOG_WARN("Could not open hardware codec");
        return false;
    }

    LOG_INFO("Hardware decoder initialized: {} (Format: {})", codec->name, av_get_pix_fmt_name(hw_pix_fmt_));
    return true;
}

int hard_decoder_backend::send_packet(const AVPacket *pkt)
{
    if (ctx_ == nullptr)
    {
        return -1;
    }
    return avcodec_send_packet(ctx_, pkt);
}

int hard_decoder_backend::receive_frame(AVFrame *frame)
{
    if (ctx_ == nullptr)
    {
        return -1;
    }
    return avcodec_receive_frame(ctx_, frame);
}

void hard_decoder_backend::flush()
{
    if (ctx_ != nullptr)
    {
        avcodec_flush_buffers(ctx_);
    }
}

const char *hard_decoder_backend::name() const { return "Hardware"; }

AVCodecContext *hard_decoder_backend::get_context() const { return ctx_; }

enum AVPixelFormat hard_decoder_backend::get_pixel_format() const { return hw_pix_fmt_; }

bool hard_decoder_backend::init_hw_device(const AVCodec *codec)
{
    enum AVHWDeviceType priority_list[] = {AV_HWDEVICE_TYPE_CUDA,
                                           AV_HWDEVICE_TYPE_D3D11VA,
                                           AV_HWDEVICE_TYPE_DXVA2,
                                           AV_HWDEVICE_TYPE_VAAPI,
                                           AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
                                           AV_HWDEVICE_TYPE_NONE};

    for (int i = 0; priority_list[i] != AV_HWDEVICE_TYPE_NONE; i++)
    {
        enum AVHWDeviceType type = priority_list[i];

        for (int j = 0;; j++)
        {
            const AVCodecHWConfig *config = avcodec_get_hw_config(codec, j);
            if (config == nullptr)
            {
                break;
            }

            if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0 && config->device_type == type)
            {
                if (av_hwdevice_ctx_create(&hw_device_ctx_, type, nullptr, nullptr, 0) >= 0)
                {
                    hw_pix_fmt_ = config->pix_fmt;
                    LOG_INFO("Hardware device created: {}", av_hwdevice_get_type_name(type));
                    return true;
                }
            }
        }
    }
    return false;
}

enum AVPixelFormat hard_decoder_backend::get_hw_format_impl(const enum AVPixelFormat *pix_fmts) const
{
    const enum AVPixelFormat *p;
    for (p = pix_fmts; *p != -1; p++)
    {
        if (*p == hw_pix_fmt_)
        {
            return *p;
        }
    }
    return AV_PIX_FMT_NONE;
}
