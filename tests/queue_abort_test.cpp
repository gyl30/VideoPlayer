#include <atomic>
#include <chrono>
#include <cstdio>
#include <dlfcn.h>
#include <thread>
#include "safe_queue.h"

#ifdef __GLIBC__

using wait_function = int (*)(pthread_cond_t *, pthread_mutex_t *);
wait_function real_wait = nullptr;
std::atomic<bool> gate{true};
std::atomic<bool> entering_wait{false};
std::atomic<bool> release_wait{false};

extern "C" int pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex)
{
    // 卡在条件检查之后、原子解锁等待之前，复现取消通知丢失。
    if (gate.exchange(false))
    {
        entering_wait.store(true);
        while (!release_wait.load())
        {
            std::this_thread::yield();
        }
    }
    return real_wait(condition, mutex);
}

int main()
{
    real_wait = reinterpret_cast<wait_function>(dlvsym(RTLD_NEXT, "pthread_cond_wait", "GLIBC_2.3.2"));
    safe_queue<int> queue;
    std::atomic<bool> popped{false};
    std::atomic<bool> aborted{false};
    std::thread consumer([&]()
    {
        int value = 0;
        (void)queue.pop(value);
        popped.store(true);
    });
    while (!entering_wait.load())
    {
        std::this_thread::yield();
    }
    std::thread canceller([&]()
    {
        queue.abort();
        aborted.store(true);
    });
    const auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    while (!aborted.load() && std::chrono::steady_clock::now() < limit)
    {
        std::this_thread::yield();
    }
    release_wait.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const bool woke = popped.load();
    queue.abort();
    canceller.join();
    consumer.join();
    std::printf("abort 唤醒空队列等待线程: %s\n", woke ? "通过" : "失败（通知丢失）");
    return woke ? 0 : 1;
}

#else
int main() { return 77; }
#endif
