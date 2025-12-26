#ifndef VIDEO_SYNC_THREAD_H
#define VIDEO_SYNC_THREAD_H

#include <QThread>
#include "av_clock.h"
#include "safe_queue.h"
#include "video_scaler.h"
#include "media_objects.h"

class video_sync_thread : public QThread
{
    Q_OBJECT

   public:
    video_sync_thread(safe_queue<std::shared_ptr<media_frame>> *frame_queue,
                      safe_queue<std::shared_ptr<media_packet>> *packet_queue,
                      AVRational tb,
                      av_clock *clk,
                      QObject *parent = nullptr);

   public:
    void stop();
    void paused(bool p);

   protected:
    void run() override;

   signals:
    void frame_ready(std::shared_ptr<media_frame> frame);

   private:
    bool stop_ = false;
    video_scaler scaler_;
    av_clock *clock_ = nullptr;
    AVRational time_base_{0, 1};
    std::atomic<bool> paused_{false};
    safe_queue<std::shared_ptr<media_frame>> *frame_queue_ = nullptr;
    safe_queue<std::shared_ptr<media_packet>> *packet_queue_ = nullptr;
};

#endif
