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
#include <functional>
#include "video_frame.h"
#include "frame_queue.h"
#include "packet_queue.h"
#include "av_clock.h"
#include "decoder_backend.h"

struct AVFormatContext;
struct AVCodecContext;
struct SwrContext;
struct SwsContext;
struct AVStream;
struct AVBufferRef;
struct AVCodec;
struct AVCodecParameters;

class audio_output;

class video_decoder : public QObject
{
    Q_OBJECT
   public:
    using RenderCallback = std::function<void(Frame *frame)>;

    explicit video_decoder(QObject *parent = nullptr);
    ~video_decoder() override;

   public:
    void open_async(const QString &file_path, int64_t op_id);
    void start(int64_t op_id);
    void stop(int64_t op_id);
    void seek_async(double pos, int64_t op_id);

    double get_duration() const;
    double get_master_clock();
    double get_frame_rate() const;

    bool is_stopping() const { return abort_request_; }
    int current_hw_pix_fmt() const;

    frame_queue video_frame_queue_;
    packet_queue video_packet_queue_;

   signals:
    void media_info_loaded(int64_t op_id, double duration);
    void seek_finished(int64_t op_id, double target_pos, bool success);
    void error_occurred(int64_t op_id, QString msg);

   public:
    void set_render_callback(RenderCallback cb) { render_cb_ = std::move(cb); }

   private:
    void open_thread_func(const QString &file, int64_t op_id);
    void demux_thread_func();
    void video_thread_func();
    void audio_thread_func();
    void render_thread_func();

    bool init_video_decoder(AVFormatContext *fmt_ctx, int64_t op_id);
    bool init_audio_decoder(AVFormatContext *fmt_ctx, int64_t op_id);
    void free_resources();

    bool open_codec_context(const AVCodec *codec, AVCodecParameters *par);
    void audio_callback_impl(uint8_t *stream, int len);

    void notify_packet_consumed();

    static int decode_interrupt_cb(void *ctx);

   private:
    QString file_;

    std::atomic<bool> abort_request_{false};
    std::atomic<bool> is_paused_{true};
    std::atomic<double> total_duration_{0.0};

    std::atomic<bool> seek_req_{false};
    std::atomic<double> seek_pos_{0.0};
    int64_t seek_op_id_ = 0;

    std::thread open_thread_;
    std::thread demux_thread_;
    std::thread video_thread_;
    std::thread audio_thread_;
    std::thread render_thread_;

    RenderCallback render_cb_;

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
    std::unique_ptr<decoder_backend> video_backend_;
    AVCodecContext *audio_ctx_ = nullptr;
    SwrContext *swr_ctx_ = nullptr;
    SwsContext *img_convert_ctx_ = nullptr;
    AVStream *video_stream_ = nullptr;
    AVStream *audio_stream_ = nullptr;

    std::unique_ptr<audio_output> audio_out_;

    int audio_buf_index_ = 0;
    double audio_current_pts_ = 0;

    int video_ctx_serial_ = -1;
    int audio_ctx_serial_ = -1;

    struct
    {
        int sample_rate = 0;
        int channels = 0;
        int format = -1;
    } last_audio_params_;

    double frame_timer_ = 0.0;
    double prev_frame_delay_ = 0.04;
};

#endif
