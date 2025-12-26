#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QTimer>
#include <QKeyEvent>
#include <thread>
#include <memory>
#include <functional>

#include "demuxer.h"
#include "decoder.h"
#include "av_clock.h"
#include "safe_queue.h"
#include "video_widget.h"
#include "media_objects.h"
#include "sdl_audio_backend.h"
#include "video_sync_thread.h"

class main_window : public QMainWindow
{
    Q_OBJECT

   public:
    explicit main_window(QWidget *parent = nullptr);
    ~main_window() override;

   protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

   private slots:
    void on_open_file();
    void on_toggle_pause();
    void on_stop_pressed();
    void on_update_ui();
    void on_seek_forward();
    void on_seek_backward();
    void on_slider_pressed();
    void on_slider_released();
    void on_volume_changed(int value);
    void on_toggle_fullscreen();

   private:
    void stop_play();
    bool start_play(const std::string &filepath);
    void do_seek_relative(double seconds);
    void init_styles();

   private:
    video_widget *video_widget_ = nullptr;

    QWidget *control_panel_ = nullptr;

    QPushButton *btn_backward_ = nullptr;
    QPushButton *btn_play_pause_ = nullptr;
    QPushButton *btn_forward_ = nullptr;

    QPushButton *btn_stop_ = nullptr;
    QPushButton *btn_menu_ = nullptr;

    QSlider *slider_seek_ = nullptr;
    QLabel *lbl_time_ = nullptr;

    QLabel *lbl_vol_icon_low_ = nullptr;
    QSlider *slider_volume_ = nullptr;
    QLabel *lbl_vol_icon_high_ = nullptr;

    QPushButton *btn_fullscreen_ = nullptr;

    QTimer *ui_timer_ = nullptr;

    bool playing_ = false;
    bool paused_ = false;
    double duration_ = 0.0;
    std::thread demux_thread_;
    std::thread video_decoder_thread_;
    std::thread audio_decoder_thread_;
    std::unique_ptr<av_clock> clock_;
    std::unique_ptr<demuxer> demuxer_;
    std::unique_ptr<decoder> video_decoder_;
    std::unique_ptr<decoder> audio_decoder_;
    std::unique_ptr<video_sync_thread> sync_thread_;
    std::unique_ptr<sdl_audio_backend> audio_backend_;
    std::unique_ptr<safe_queue<std::shared_ptr<media_packet>>> video_pkt_queue_;
    std::unique_ptr<safe_queue<std::shared_ptr<media_packet>>> audio_pkt_queue_;
    std::unique_ptr<safe_queue<std::shared_ptr<media_frame>>> video_frame_queue_;
    std::unique_ptr<safe_queue<std::shared_ptr<media_frame>>> audio_frame_queue_;
};

#endif
