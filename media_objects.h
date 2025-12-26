#ifndef MEDIA_OBJECTS_H
#define MEDIA_OBJECTS_H

#include <memory>
#include <utility>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

class media_packet
{
   public:
    media_packet() { pkt_ = av_packet_alloc(); }

    ~media_packet()
    {
        if (pkt_ != nullptr)
        {
            av_packet_free(&pkt_);
        }
    }

    static std::shared_ptr<media_packet> create_flush()
    {
        auto pkt = std::make_shared<media_packet>();
        pkt->flush_ = true;
        return pkt;
    }

    [[nodiscard]] bool flush() const { return flush_; }

    media_packet(const media_packet &) = delete;
    media_packet &operator=(const media_packet &) = delete;

    media_packet(media_packet &&other) noexcept
    {
        pkt_ = other.pkt_;
        flush_ = other.flush_;
        serial_ = other.serial_;
        other.pkt_ = nullptr;
    }

    media_packet &operator=(media_packet &&other) noexcept
    {
        if (this != &other)
        {
            if (pkt_ != nullptr)
            {
                av_packet_free(&pkt_);
            }
            pkt_ = other.pkt_;
            flush_ = other.flush_;
            serial_ = other.serial_;
            other.pkt_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] AVPacket *raw() const { return pkt_; }

    void set_serial(int s) { serial_ = s; }
    [[nodiscard]] int serial() const { return serial_; }

   private:
    bool flush_ = false;
    int serial_ = 0;
    AVPacket *pkt_ = nullptr;
};

class media_frame
{
   public:
    media_frame() { frame_ = av_frame_alloc(); }

    ~media_frame()
    {
        if (frame_ != nullptr)
        {
            av_frame_free(&frame_);
        }
    }

    static std::shared_ptr<media_frame> create_flush()
    {
        auto frame = std::make_shared<media_frame>();
        frame->flush_ = true;
        return frame;
    }

    [[nodiscard]] bool flush() const { return flush_; }

    media_frame(const media_frame &) = delete;
    media_frame &operator=(const media_frame &) = delete;

    media_frame(media_frame &&other) noexcept
    {
        frame_ = other.frame_;
        flush_ = other.flush_;
        serial_ = other.serial_;
        other.frame_ = nullptr;
    }

    media_frame &operator=(media_frame &&other) noexcept
    {
        if (this != &other)
        {
            if (frame_ != nullptr)
            {
                av_frame_free(&frame_);
            }
            frame_ = other.frame_;
            flush_ = other.flush_;
            serial_ = other.serial_;
            other.frame_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] AVFrame *raw() const { return frame_; }

    void set_serial(int s) { serial_ = s; }
    [[nodiscard]] int serial() const { return serial_; }

   private:
    bool flush_ = false;
    int serial_ = 0;
    AVFrame *frame_ = nullptr;
};

#endif
