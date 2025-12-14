#ifndef FRAME_QUEUE_H
#define FRAME_QUEUE_H

#include <vector>
#include <mutex>
#include <condition_variable>
#include "video_frame.h"
#include "packet_queue.h"

class frame_queue
{
   public:
    void init(packet_queue *pktq, int max_size, bool keep_last)
    {
        pktq_ = pktq;
        max_size_ = std::min(max_size, 16);
        keep_last_ = keep_last;

        queue_.resize(max_size_);
    }

    void start()
    {
        rindex_ = 0;
        windex_ = 0;
        size_ = 0;
        rindex_shown_ = 0;
    }

    void abort()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cond_.notify_all();
    }

    Frame *peek_writable()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (size_ >= max_size_ && !pktq_->is_aborted())
        {
            cond_.wait(lock);
        }

        if (pktq_->is_aborted())
            return nullptr;

        return &queue_[windex_];
    }

    void push()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (++windex_ == max_size_)
            windex_ = 0;
        size_++;
        cond_.notify_all();
    }

    Frame *peek_readable()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (size_ - rindex_shown_ <= 0 && !pktq_->is_aborted())
        {
            cond_.wait(lock);
        }

        if (pktq_->is_aborted())
            return nullptr;

        return &queue_[(rindex_ + rindex_shown_) % max_size_];
    }

    Frame *peek_last() { return &queue_[rindex_]; }

    Frame *peek_next() { return &queue_[(rindex_ + rindex_shown_ + 1) % max_size_]; }

    void next()
    {
        if (keep_last_ && !rindex_shown_)
        {
            rindex_shown_ = 1;
            return;
        }

        av_frame_unref(queue_[rindex_].frame);

        if (++rindex_ == max_size_)
            rindex_ = 0;

        std::lock_guard<std::mutex> lock(mutex_);
        size_--;
        cond_.notify_all();
    }

    int remaining()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ - rindex_shown_;
    }

   private:
    std::vector<Frame> queue_;
    int rindex_ = 0;
    int windex_ = 0;
    int size_ = 0;
    int max_size_ = 0;
    int rindex_shown_ = 0;
    bool keep_last_ = false;

    std::mutex mutex_;
    std::condition_variable cond_;
    packet_queue *pktq_ = nullptr;
};

#endif
