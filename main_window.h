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
#include <QFrame>
#include <QTreeWidget>
#include <QTimer>
#include <QEvent>
#include <QKeyEvent>
#include <QPoint>
#include <QResizeEvent>
#include <Qt>
#include <QString>
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
#include "playlist_store.h"

class main_window : public QMainWindow
{
    Q_OBJECT

   public:
    explicit main_window(QWidget *parent = nullptr);
    ~main_window() override;

   protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

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
    void on_toggle_playlist();
    void on_play_previous();
    void on_play_next();
    void on_playlist_item_activated(QTreeWidgetItem *item, int column);

   private:
    void stop_play();
    bool start_play(const std::string &filepath);
    void do_seek_relative(double seconds);
    void init_styles();
    void toggle_window_maximized();
    void update_title_maximize_button();
    void play_playlist_item(const QString &playlist_id, int row);
    void play_playlist_row(int row);
    void update_playlist_buttons();
    void finish_playback();
    void refresh_playlist_view();
    void set_active_playlist(const QString &playlist_id);
    QString active_playlist_id() const;
    void show_playlist_context_menu(const QPoint &pos);
    void open_playlist_management_dialog();
    void apply_playlist_management_changes(const playlist_store &store);
    bool is_playlist_item(const QTreeWidgetItem *item) const;
    bool is_playlist_file_item(const QTreeWidgetItem *item) const;
    QString playlist_id_for_item(const QTreeWidgetItem *item) const;
    int playlist_row_for_item(const QTreeWidgetItem *item) const;
    QString playback_playlist_id() const;
    int playback_playlist_row() const;
    void set_media_title_text(const QString &text);
    void update_media_title_text();
    void on_title_scroll_tick();
    void update_volume_icon(int value);
    void set_playback_rate(double rate);
    void show_playback_rate_menu(const QPoint &global_pos);
    void restore_persistent_state();
    void save_persistent_state();
    void save_playlist_state();
    void save_volume_state(int value);
    void save_current_playback_progress(bool force = false);
    void restore_playback_progress(const QString &path);
    bool is_video_fullscreen() const;
    void enter_video_fullscreen();
    void exit_video_fullscreen();
    bool handle_window_resize(QObject *watched, QEvent *event);
    Qt::Edges hit_test_resize_edges(const QPoint &global_pos) const;
    static Qt::CursorShape cursor_shape_for_edges(Qt::Edges edges);

   private:
    video_widget *video_widget_ = nullptr;
    QVBoxLayout *video_frame_layout_ = nullptr;
    QWidget *video_fullscreen_window_ = nullptr;
    QVBoxLayout *video_fullscreen_layout_ = nullptr;

    QWidget *title_bar_ = nullptr;
    QWidget *title_drag_area_ = nullptr;
    QLabel *lbl_media_title_ = nullptr;
    QPushButton *btn_title_minimize_ = nullptr;
    QPushButton *btn_title_maximize_ = nullptr;
    QPushButton *btn_title_close_ = nullptr;
    QPushButton *btn_playlist_ = nullptr;
    QPushButton *btn_playback_rate_ = nullptr;
    QPushButton *btn_sequential_playback_ = nullptr;

    QFrame *video_frame_ = nullptr;
    QFrame *playlist_panel_ = nullptr;
    QTreeWidget *playlist_view_ = nullptr;
    QLabel *lbl_playlist_count_ = nullptr;

    QWidget *control_panel_ = nullptr;

    QPushButton *btn_backward_ = nullptr;
    QPushButton *btn_play_pause_ = nullptr;
    QPushButton *btn_forward_ = nullptr;

    QPushButton *btn_stop_ = nullptr;

    QSlider *slider_seek_ = nullptr;
    QLabel *lbl_time_ = nullptr;

    QLabel *lbl_vol_icon_low_ = nullptr;
    class volume_meter *volume_meter_ = nullptr;

    QTimer *ui_timer_ = nullptr;
    QTimer *title_scroll_timer_ = nullptr;

    bool dragging_title_bar_ = false;
    bool closing_ = false;
    QPoint drag_start_global_pos_;
    QPoint drag_start_window_pos_;
    int drag_press_window_x_ = 0;
    QRect fullscreen_restore_geometry_;
    bool fullscreen_restore_maximized_ = false;
    QString current_media_path_;
    QString current_playback_playlist_id_;
    QString media_title_full_text_ = "视频播放器";
    int media_title_scroll_offset_ = 0;
    int last_saved_progress_second_ = -1;
    int current_playback_row_ = -1;
    double playback_rate_ = 1.0;
    playlist_store playlist_store_;

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
