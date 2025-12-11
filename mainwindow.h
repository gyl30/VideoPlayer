#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QImage>

class video_decoder;

class main_window : public QMainWindow
{
    Q_OBJECT

   public:
    explicit main_window(QWidget *parent = nullptr);
    ~main_window() override;

   private slots:
    void on_open_action_triggered();
    void on_frame_ready(const QImage &image);

   private:
    void setup_ui();

    video_decoder *decoder_ = nullptr;
    QLabel *display_ = nullptr;
};

#endif
