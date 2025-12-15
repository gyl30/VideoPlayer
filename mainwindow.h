#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSlider>
#include <QLabel>
#include <QTimer>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <atomic>

class video_decoder;
class video_widget;

class main_window : public QMainWindow
{
    Q_OBJECT

   public:
    explicit main_window(QWidget *parent = nullptr);
    ~main_window() override;

   private slots:
    void on_open_action_triggered();
    void on_timer_timeout();

    void on_slider_pressed();
    void on_slider_released();
    void on_slider_moved(int value);

    void on_media_info_loaded(int64_t op_id, double duration);
    void on_seek_finished(int64_t op_id, double target_pos, bool success);
    void on_decoder_error(int64_t op_id, QString msg);

   private:
    void setup_ui();
    int64_t get_next_op_id();
    QString format_time(double seconds);

   private:
    video_decoder *decoder_ = nullptr;
    video_widget *video_widget_ = nullptr;

    QWidget *central_widget_ = nullptr;
    QVBoxLayout *main_layout_ = nullptr;
    QHBoxLayout *control_layout_ = nullptr;

    QSlider *seek_slider_ = nullptr;
    QLabel *current_time_lbl_ = nullptr;
    QLabel *total_time_lbl_ = nullptr;

    QTimer *update_timer_ = nullptr;
    bool is_seeking_ = false;

    std::atomic<int64_t> current_op_id_{1000};
};

#endif
