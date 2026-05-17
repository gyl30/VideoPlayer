#include "decoder.h"
#include "log.h"
#include <thread>
#include <chrono>

decoder::~decoder()
{
    LOG_INFO("decoder destroying name {}", name_);
    stop();
    close_codec_context();
    if (codec_par_ != nullptr)
    {
        avcodec_parameters_free(&codec_par_);
    }
}

void decoder::stop() { aborted_.store(true); }

AVPixelFormat decoder::get_hw_format(AVCodecContext *ctx, const AVPixelFormat *pix_fmts)
{
    auto *self = static_cast<decoder *>(ctx->opaque);
    if (self == nullptr)
    {
        return AV_PIX_FMT_NONE;
    }

    for (const AVPixelFormat *fmt = pix_fmts; *fmt != AV_PIX_FMT_NONE; ++fmt)
    {
        if (*fmt == self->hw_pix_fmt_)
        {
            return *fmt;
        }
    }

    LOG_WARN("decoder hardware pixel format not offered by codec name {}", self->name_);
    return pix_fmts[0];
}

void decoder::close_codec_context()
{
    if (codec_ctx_ != nullptr)
    {
        avcodec_free_context(&codec_ctx_);
    }
    hw_pix_fmt_ = AV_PIX_FMT_NONE;
    hw_device_type_ = AV_HWDEVICE_TYPE_NONE;
    using_hw_decode_ = false;
}

bool decoder::open_codec_context(bool try_hardware)
{
    close_codec_context();

    if (codec_par_ == nullptr)
    {
        return false;
    }

    const AVCodec *codec = avcodec_find_decoder(codec_par_->codec_id);
    if (codec == nullptr)
    {
        LOG_ERROR("decoder find decoder failed name {}", name_);
        return false;
    }

    if (try_hardware && video_decoder_)
    {
        for (int index = 0;; ++index)
        {
            const AVCodecHWConfig *config = avcodec_get_hw_config(codec, index);
            if (config == nullptr)
            {
                break;
            }

            if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) == 0)
            {
                continue;
            }

            AVBufferRef *device_ctx = nullptr;
            if (av_hwdevice_ctx_create(&device_ctx, config->device_type, nullptr, nullptr, 0) < 0)
            {
                continue;
            }

            AVCodecContext *context = avcodec_alloc_context3(codec);
            if (context == nullptr)
            {
                av_buffer_unref(&device_ctx);
                continue;
            }

            if (avcodec_parameters_to_context(context, codec_par_) < 0)
            {
                avcodec_free_context(&context);
                av_buffer_unref(&device_ctx);
                continue;
            }

            hw_pix_fmt_ = config->pix_fmt;
            hw_device_type_ = config->device_type;
            context->opaque = this;
            context->get_format = get_hw_format;
            context->hw_device_ctx = av_buffer_ref(device_ctx);
            if (context->hw_device_ctx == nullptr)
            {
                avcodec_free_context(&context);
                av_buffer_unref(&device_ctx);
                continue;
            }

            if (avcodec_open2(context, codec, nullptr) >= 0)
            {
                codec_ctx_ = context;
                using_hw_decode_ = true;
                LOG_INFO("decoder hardware decode enabled name {} device {}", name_, av_hwdevice_get_type_name(config->device_type));
                av_buffer_unref(&device_ctx);
                return true;
            }

            avcodec_free_context(&context);
            av_buffer_unref(&device_ctx);
        }

        LOG_WARN("decoder hardware decode unavailable, falling back to software name {}", name_);
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (codec_ctx_ == nullptr)
    {
        LOG_ERROR("decoder alloc context failed name {}", name_);
        return false;
    }

    if (avcodec_parameters_to_context(codec_ctx_, codec_par_) < 0)
    {
        LOG_ERROR("decoder parameters to context failed name {}", name_);
        close_codec_context();
        return false;
    }

    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0)
    {
        LOG_ERROR("decoder avcodec open2 failed name {}", name_);
        close_codec_context();
        return false;
    }

    LOG_INFO("decoder software decode enabled name {}", name_);
    return true;
}

bool decoder::reopen_software_decoder()
{
    if (!using_hw_decode_)
    {
        return false;
    }

    LOG_WARN("decoder switching to software fallback name {}", name_);
    return open_codec_context(false);
}

bool decoder::open(const AVCodecParameters *par,
                   safe_queue<std::shared_ptr<media_packet>> *packet_queue,
                   safe_queue<std::shared_ptr<media_frame>> *frame_queue,
                   const std::string &name,
                   bool try_hardware_decode)
{
    packet_queue_ = packet_queue;
    frame_queue_ = frame_queue;
    name_ = name;
    video_decoder_ = (par != nullptr && par->codec_type == AVMEDIA_TYPE_VIDEO);

    if (par == nullptr)
    {
        return false;
    }
    LOG_INFO("decoder opening name {} codec id {}", name, avcodec_get_name(par->codec_id));

    if (codec_par_ == nullptr)
    {
        codec_par_ = avcodec_parameters_alloc();
    }
    if (codec_par_ == nullptr || avcodec_parameters_copy(codec_par_, par) < 0)
    {
        LOG_ERROR("decoder copy codec parameters failed name {}", name);
        return false;
    }

    if (!open_codec_context(video_decoder_ && try_hardware_decode))
    {
        return false;
    }
    LOG_INFO("decoder open success name {}", name);
    return true;
}

void decoder::run()
{
    if (codec_ctx_ == nullptr || packet_queue_ == nullptr)
    {
        LOG_WARN("decoder run called with invalid state name {}", name_);
        return;
    }

    LOG_INFO("decoder loop started name {}", name_);

    std::shared_ptr<media_packet> pkt;
    int current_serial = 0;

    aborted_.store(false);

    while (!aborted_.load())
    {
        if (!packet_queue_->pop(pkt))
        {
            if (aborted_.load())
            {
                LOG_INFO("decoder packet queue popped false exiting name {}", name_);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (pkt != nullptr)
        {
            current_serial = pkt->serial();
        }

        if (pkt != nullptr && pkt->flush())
        {
            LOG_INFO("decoder received flush packet flushing buffers name {}", name_);
            avcodec_flush_buffers(codec_ctx_);
            if (frame_queue_ != nullptr)
            {
                frame_queue_->clear();
                auto flush_frame = media_frame::create_flush();
                flush_frame->set_serial(current_serial);
                frame_queue_->push(flush_frame);
            }
            continue;
        }

        AVPacket *raw_pkt = (pkt != nullptr) ? pkt->raw() : nullptr;
        bool frame_emitted_for_packet = false;

        int ret = avcodec_send_packet(codec_ctx_, raw_pkt);
        if (ret < 0)
        {
            if (raw_pkt != nullptr && using_hw_decode_ && reopen_software_decoder())
            {
                ret = avcodec_send_packet(codec_ctx_, raw_pkt);
            }
        }
        if (ret < 0)
        {
            LOG_ERROR("decoder avcodec send packet failed code {} name {}", ret, name_);
            continue;
        }

        while (ret >= 0)
        {
            auto frame = std::make_shared<media_frame>();
            ret = avcodec_receive_frame(codec_ctx_, frame->raw());

            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                break;
            }
            if (ret < 0)
            {
                if (raw_pkt != nullptr && using_hw_decode_ && !frame_emitted_for_packet && reopen_software_decoder())
                {
                    ret = avcodec_send_packet(codec_ctx_, raw_pkt);
                    if (ret < 0)
                    {
                        LOG_ERROR("decoder avcodec resend packet failed after fallback code {} name {}", ret, name_);
                        break;
                    }
                    continue;
                }

                LOG_ERROR("decoder avcodec receive frame failed code {} name {}", ret, name_);
                break;
            }

            if (using_hw_decode_ && frame->raw()->format == hw_pix_fmt_)
            {
                auto software_frame = std::make_shared<media_frame>();
                if (av_hwframe_transfer_data(software_frame->raw(), frame->raw(), 0) < 0 ||
                    av_frame_copy_props(software_frame->raw(), frame->raw()) < 0)
                {
                    if (raw_pkt != nullptr && !frame_emitted_for_packet && reopen_software_decoder())
                    {
                        ret = avcodec_send_packet(codec_ctx_, raw_pkt);
                        if (ret < 0)
                        {
                            LOG_ERROR("decoder avcodec resend packet failed after transfer fallback code {} name {}", ret, name_);
                            break;
                        }
                        continue;
                    }

                    LOG_ERROR("decoder hardware frame transfer failed name {}", name_);
                    if (using_hw_decode_)
                    {
                        reopen_software_decoder();
                    }
                    break;
                }

                frame = software_frame;
            }

            frame->set_serial(current_serial);
            frame_emitted_for_packet = true;

            if (frame_queue_ != nullptr)
            {
                if (!frame_queue_->push(frame))
                {
                    if (aborted_.load())
                    {
                        LOG_INFO("decoder frame queue push failed exiting name {}", name_);
                        goto end_loop;
                    }
                }
            }
        }

        if (raw_pkt == nullptr)
        {
            LOG_INFO("decoder finished draining, waiting for next command name {}", name_);
        }
    }

end_loop:
    LOG_INFO("decoder loop ending name {}", name_);
    if (frame_queue_ != nullptr)
    {
        frame_queue_->push(nullptr);
    }
}
