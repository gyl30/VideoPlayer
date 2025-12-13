#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>

#include "log.h"
#include "mainwindow.h"
#include "video_widget.h"
#include "video_decoder.h"

main_window::main_window(QWidget *parent) : QMainWindow(parent)
{
    setup_ui();
    decoder_ = new video_decoder(this);

    render_timer_ = new QTimer(this);
    render_timer_->setInterval(5);
    connect(render_timer_, &QTimer::timeout, this, &main_window::on_timer_tick);
}

main_window::~main_window()
{
    render_timer_->stop();
    if (decoder_ != nullptr)
    {
        decoder_->stop();
    }
}

void main_window::setup_ui()
{
    setWindowTitle("VideoPlayer Pro");
    resize(800, 600);

    QMenu *fileMenu = menuBar()->addMenu(tr("File"));
    QAction *openAction = fileMenu->addAction(tr("Open Video"));
    openAction->setShortcut(QKeySequence::Open);

    connect(openAction, &QAction::triggered, this, &main_window::on_open_action_triggered);

    video_widget_ = new video_widget(this);
    setCentralWidget(video_widget_);
}

void main_window::on_open_action_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Video"), "", tr("Video Files (*.mp4 *.mkv *.avi *.mov *.flv);;All Files (*)"));

    if (fileName.isEmpty())
    {
        return;
    }

    LOG_INFO("selected file {}", fileName.toStdString());

    if (!decoder_->open(fileName))
    {
        QMessageBox::critical(this, tr("Error"), tr("Could not open video file!"));
    }
    else
    {
        render_timer_->start();
    }
}

void main_window::on_timer_tick()
{
    double master_clock = decoder_->get_master_clock();
    if (std::isnan(master_clock))
    {
        return;
    }

    VideoFramePtr frame = decoder_->get_video_frame();
    if (!frame)
    {
        return;
    }

    double diff = frame->pts - master_clock;
    double sync_threshold = 0.03;

    if (diff <= sync_threshold)
    {
        if (diff < -0.2)
        {
            decoder_->pop_video_frame();
        }
        else
        {
            video_widget_->update_frame(frame);
            decoder_->pop_video_frame();
        }
    }
}
