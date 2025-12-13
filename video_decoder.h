#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include <QThread>
#include <QString>
#include <atomic>
#include <memory>
#include "video_frame.h"

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;
struct AVStream;

class audio_output;

class video_decoder : public QThread
{
    Q_OBJECT
   public:
    explicit video_decoder(QObject *parent = nullptr);
    ~video_decoder() override;

   public:
    bool open(const QString &file_path);
    void stop();

   signals:
    void frame_ready(VideoFramePtr frame);

   protected:
    void run() override;

   private:
    bool init_video_decoder(AVFormatContext *fmt_ctx);
    bool init_audio_decoder(AVFormatContext *fmt_ctx);
    void process_video_packet(AVPacket *pkt, AVFrame *frame);
    void process_audio_packet(AVPacket *pkt, AVFrame *frame);
    void free_resources();
    void synchronize_video(double pts);
    double get_master_clock();

   private:
    QString file_;
    std::atomic<bool> stop_;
    int video_index_ = -1;
    int audio_index_ = -1;
    AVCodecContext *video_ctx_ = nullptr;
    AVCodecContext *audio_ctx_ = nullptr;
    SwrContext *swr_ctx_ = nullptr;
    AVStream *video_stream_ = nullptr;
    AVStream *audio_stream_ = nullptr;
    std::unique_ptr<audio_output> audio_out_;
    int64_t video_start_system_time_ = 0;
};

#endif
