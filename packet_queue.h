#ifndef PACKET_QUEUE_H
#define PACKET_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

extern "C"
{
#include <libavcodec/avcodec.h>
}

struct PacketData
{
    AVPacket *pkt;
    int serial;
};

class packet_queue
{
   public:
    packet_queue() { abort_request_ = 1; }

    ~packet_queue() { flush(); }

    void start()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        abort_request_ = 0;
        serial_++;
        duration_ = 0;
        cond_.notify_all();
    }

    void abort()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        abort_request_ = 1;
        cond_.notify_all();
    }

    bool is_aborted() const { return abort_request_ != 0; }

    void flush()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty())
        {
            AVPacket *pkt = queue_.front().pkt;
            av_packet_free(&pkt);
            queue_.pop();
        }
        size_ = 0;
        duration_ = 0;
        serial_++;
        cond_.notify_all();
    }

    int put(AVPacket *pkt)
    {
        AVPacket *pkt_ref = av_packet_alloc();
        if (!pkt_ref)
            return -1;

        av_packet_move_ref(pkt_ref, pkt);

        std::lock_guard<std::mutex> lock(mutex_);
        if (abort_request_)
        {
            av_packet_free(&pkt_ref);
            return -1;
        }

        PacketData data;
        data.pkt = pkt_ref;
        data.serial = serial_;

        queue_.push(data);
        size_ += pkt_ref->size;
        duration_ += pkt_ref->duration;
        cond_.notify_one();
        return 0;
    }

    int get(AVPacket *pkt, int *serial, bool block)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (true)
        {
            if (abort_request_)
                return -1;

            if (!queue_.empty())
            {
                PacketData data = queue_.front();
                queue_.pop();
                size_ -= data.pkt->size;
                duration_ -= data.pkt->duration;

                av_packet_move_ref(pkt, data.pkt);
                if (serial)
                    *serial = data.serial;
                av_packet_free(&data.pkt);
                return 1;
            }

            if (!block)
                return 0;
            cond_.wait(lock);
        }
    }

    int serial()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return serial_;
    }

    int size() const { return size_; }

    int count()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(queue_.size());
    }

   private:
    std::queue<PacketData> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<int> abort_request_{1};
    int serial_ = 0;
    int size_ = 0;
    int64_t duration_ = 0;
};

#endif
