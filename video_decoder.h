#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include <QObject>
#include <QString>
#include <atomic>
#include <thread>
#include <memory>
#include "video_frame.h"
#include "frame_queue.h"
#include "packet_queue.h"

struct AVFormatContext;
struct AVCodecContext;
struct SwrContext;
struct AVStream;

class audio_output;

class video_decoder : public QObject
{
    Q_OBJECT
   public:
    explicit video_decoder(QObject *parent = nullptr);
    ~video_decoder() override;

    bool open(const QString &file_path);
    void stop();

    VideoFramePtr get_video_frame();
    double get_master_clock();
    void pop_video_frame();

   private:
    void demux_thread_func();
    void video_thread_func();
    void audio_thread_func();

    bool init_video_decoder(AVFormatContext *fmt_ctx);
    bool init_audio_decoder(AVFormatContext *fmt_ctx);
    void free_resources();

   private:
    QString file_;
    std::atomic<bool> stop_{false};

    std::thread demux_thread_;
    std::thread video_thread_;
    std::thread audio_thread_;

    packet_queue video_packet_queue_;
    packet_queue audio_packet_queue_;
    frame_queue video_frame_queue_;

    int video_index_ = -1;
    int audio_index_ = -1;

    AVCodecContext *video_ctx_ = nullptr;
    AVCodecContext *audio_ctx_ = nullptr;
    SwrContext *swr_ctx_ = nullptr;
    AVStream *video_stream_ = nullptr;
    AVStream *audio_stream_ = nullptr;

    std::unique_ptr<audio_output> audio_out_;
};

#endif
