#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

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
    void video_refresh();

   private:
    void setup_ui();
    void schedule_refresh(int delay) const;

   private:
    video_decoder *decoder_ = nullptr;
    video_widget *video_widget_ = nullptr;
    double frame_timer_ = 0.0;
    double prev_frame_delay_ = 0.04;
};

#endif
