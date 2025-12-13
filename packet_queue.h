#ifndef PACKET_QUEUE_H
#define PACKET_QUEUE_H

#include <queue>
#include <mutex>
#include <atomic>
#include <condition_variable>

extern "C"
{
#include <libavcodec/avcodec.h>
}

class packet_queue
{
   public:
    packet_queue() = default;
    ~packet_queue() { clear(); }

    void push(AVPacket *pkt)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(pkt);
        cond_.notify_one();
        size_ += pkt->size;
    }

    AVPacket *pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return !queue_.empty() || abort_; });
        if (abort_ || queue_.empty())
        {
            return nullptr;
        }
        AVPacket *pkt = queue_.front();
        queue_.pop();
        size_ -= pkt->size;
        return pkt;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty())
        {
            AVPacket *pkt = queue_.front();
            queue_.pop();
            av_packet_free(&pkt);
        }
        size_ = 0;
    }

    void abort()
    {
        abort_ = true;
        cond_.notify_all();
    }

    void start() { abort_ = false; }

    int size() const { return size_; }

    int count()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(queue_.size());
    }

   private:
    std::queue<AVPacket *> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<bool> abort_{false};
    std::atomic<int> size_{0};
};

#endif
