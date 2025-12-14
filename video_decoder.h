#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include <QObject>
#include <QString>
#include <atomic>
#include <thread>
#include <memory>
#include <vector>
#include <condition_variable>
#include <mutex>
#include "video_frame.h"
#include "frame_queue.h"
#include "packet_queue.h"
#include "av_clock.h"

struct AVFormatContext;
struct AVCodecContext;
struct SwrContext;
struct AVStream;
struct AVBufferRef;
struct AVCodec;
struct AVCodecParameters;

class audio_output;

class video_decoder : public QObject
{
    Q_OBJECT
   public:
    explicit video_decoder(QObject *parent = nullptr);
    ~video_decoder() override;

   public:
    bool open(const QString &file_path);
    void stop();
    void seek(double pos);
    double get_duration() const;
    double get_master_clock();
    double get_frame_rate() const;

    int current_hw_pix_fmt() const { return hw_pix_fmt_; }

    frame_queue video_frame_queue_;
    packet_queue video_packet_queue_;

   private:
    void demux_thread_func();
    void video_thread_func();
    void audio_thread_func();

    bool init_video_decoder(AVFormatContext *fmt_ctx);
    bool init_audio_decoder(AVFormatContext *fmt_ctx);
    void free_resources();

    bool init_hw_decoder(const AVCodec *codec);
    bool open_codec_context(const AVCodec *codec, AVCodecParameters *par, bool try_hw);
    void audio_callback_impl(uint8_t *stream, int len);

    void notify_packet_consumed();

   private:
    QString file_;
    std::atomic<bool> stop_{false};

    std::thread demux_thread_;
    std::thread video_thread_;
    std::thread audio_thread_;

    packet_queue audio_packet_queue_;
    frame_queue audio_frame_queue_;

    av_clock vid_clk_;
    av_clock aud_clk_;
    av_clock ext_clk_;

    std::mutex continue_read_mutex_;
    std::condition_variable continue_read_cv_;

    int video_index_ = -1;
    int audio_index_ = -1;

    AVFormatContext *fmt_ctx_ = nullptr;
    AVCodecContext *video_ctx_ = nullptr;
    AVCodecContext *audio_ctx_ = nullptr;
    SwrContext *swr_ctx_ = nullptr;
    AVStream *video_stream_ = nullptr;
    AVStream *audio_stream_ = nullptr;

    AVBufferRef *hw_device_ctx_ = nullptr;
    int hw_pix_fmt_ = -1;
    bool is_hw_decoding_ = false;

    std::unique_ptr<audio_output> audio_out_;

    int audio_buf_index_ = 0;
    double audio_current_pts_ = 0;
};

#endif
