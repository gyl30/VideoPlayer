#ifndef FRAME_QUEUE_H
#define FRAME_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include "video_frame.h"

class frame_queue
{
   public:
    void push(const VideoFramePtr& frame)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_full_.wait(lock, [this] { return queue_.size() < max_size_ || abort_; });
        if (abort_)
        {
            return;
        }
        queue_.push(frame);
        cond_empty_.notify_one();
    }

    VideoFramePtr pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty())
        {
            return nullptr;
        }
        VideoFramePtr frame = queue_.front();
        queue_.pop();
        cond_full_.notify_one();
        return frame;
    }

    VideoFramePtr peek()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.empty())
        {
            return nullptr;
        }
        return queue_.front();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<VideoFramePtr> empty;
        std::swap(queue_, empty);
        cond_full_.notify_all();
    }

    void abort()
    {
        abort_ = true;
        cond_full_.notify_all();
        cond_empty_.notify_all();
    }

    void start() { abort_ = false; }

    bool empty()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

   private:
    std::queue<VideoFramePtr> queue_;
    std::mutex mutex_;
    std::condition_variable cond_empty_;
    std::condition_variable cond_full_;
    std::atomic<bool> abort_{false};
    size_t max_size_ = 16;
};

#endif
