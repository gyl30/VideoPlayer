#ifndef SAFE_QUEUE_H
#define SAFE_QUEUE_H

#include <queue>
#include <mutex>
#include <atomic>
#include <condition_variable>

template <typename T>
class safe_queue
{
   public:
    explicit safe_queue(size_t max_size = 100) : max_size_(max_size) {}

    safe_queue(const safe_queue &) = delete;
    safe_queue &operator=(const safe_queue &) = delete;

    ~safe_queue() { abort(); }

    bool push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        cond_not_full_.wait(lock, [this] { return queue_.size() < max_size_ || abort_flag_.load(); });

        if (abort_flag_.load())
        {
            return false;
        }

        queue_.push(std::move(value));
        cond_not_empty_.notify_one();
        return true;
    }

    [[nodiscard]] bool pop(T &out_value)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        cond_not_empty_.wait(lock, [this] { return !queue_.empty() || abort_flag_.load(); });

        if (abort_flag_.load())
        {
            return false;
        }

        out_value = std::move(queue_.front());
        queue_.pop();
        cond_not_full_.notify_one();

        return true;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<T> empty;
        std::swap(queue_, empty);
        cond_not_full_.notify_all();
    }

    void abort()
    {
        abort_flag_.store(true);
        cond_not_empty_.notify_all();
        cond_not_full_.notify_all();
    }

    void reset() { abort_flag_.store(false); }

    [[nodiscard]] size_t size()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    [[nodiscard]] bool empty()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    [[nodiscard]] int serial() const { return serial_.load(); }

    void add_serial() { serial_.fetch_add(1); }

   private:
    std::queue<T> queue_;
    std::mutex mutex_;
    size_t max_size_{0};
    std::atomic<bool> abort_flag_{false};
    std::condition_variable cond_not_full_;
    std::condition_variable cond_not_empty_;
    std::atomic<int> serial_{0};
};

#endif
