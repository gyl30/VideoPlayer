#include "log.h"
#include "decoder.h"

decoder::~decoder()
{
    LOG_INFO("decoder destroying name {}", name_);
    stop();
    if (codec_ctx_ != nullptr)
    {
        avcodec_free_context(&codec_ctx_);
    }
}

void decoder::stop() { aborted_.store(true); }

bool decoder::open(const AVCodecParameters *par,
                   safe_queue<std::shared_ptr<media_packet>> *packet_queue,
                   safe_queue<std::shared_ptr<media_frame>> *frame_queue,
                   const std::string &name)
{
    packet_queue_ = packet_queue;
    frame_queue_ = frame_queue;
    name_ = name;

    LOG_INFO("decoder opening name {} codec id {}", name, avcodec_get_name(par->codec_id));

    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (codec == nullptr)
    {
        LOG_ERROR("decoder find decoder failed name {}", name);
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (codec_ctx_ == nullptr)
    {
        LOG_ERROR("decoder alloc context failed name {}", name);
        return false;
    }

    if (avcodec_parameters_to_context(codec_ctx_, par) < 0)
    {
        LOG_ERROR("decoder parameters to context failed name {}", name);
        return false;
    }
    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0)
    {
        LOG_ERROR("decoder avcodec open2 failed name {}", name);
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

        int ret = avcodec_send_packet(codec_ctx_, raw_pkt);
        if (ret < 0)
        {
            LOG_ERROR("decoder avcodec send packet failed code {} name {}", ret, name_);
            break;
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
                LOG_ERROR("decoder avcodec receive frame failed code {} name {}", ret, name_);
                goto end_loop;
            }

            frame->set_serial(current_serial);

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
            LOG_INFO("decoder received null packet finishing name {}", name_);
            break;
        }
    }

end_loop:
    LOG_INFO("decoder loop ending name {}", name_);
    if (frame_queue_ != nullptr)
    {
        frame_queue_->push(nullptr);
    }
}
