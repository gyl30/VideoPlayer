#include "mainwindow.h"
#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>
#include "log.h"
#include "video_widget.h"
#include "video_decoder.h"
#include "frame_queue.h"

extern "C"
{
#include <libavutil/time.h>
}

main_window::main_window(QWidget *parent) : QMainWindow(parent)
{
    setup_ui();
    decoder_ = new video_decoder(this);
}

main_window::~main_window()
{
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

void main_window::schedule_refresh(int delay) const { QTimer::singleShot(delay, this, &main_window::video_refresh); }

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
        frame_timer_ = static_cast<double>(av_gettime_relative()) / 1000000.0;
        prev_frame_delay_ = 0.04;
        schedule_refresh(1);
    }
}

void main_window::video_refresh()
{
    Frame *vp = decoder_->video_frame_queue_.peek_readable();
    Frame *lastvp = decoder_->video_frame_queue_.peek_last();

    if (vp == nullptr)
    {
        schedule_refresh(10);
        return;
    }

    if (vp->serial != decoder_->video_packet_queue_.serial())
    {
        decoder_->video_frame_queue_.next();
        schedule_refresh(1);
        return;
    }

    if (lastvp->serial != vp->serial)
    {
        frame_timer_ = static_cast<double>(av_gettime_relative()) / 1000000.0;
    }

    double duration = vp->duration;
    if (std::isnan(duration) || duration <= 0)
    {
        double fr = decoder_->get_frame_rate();
        if (fr > 0)
        {
            duration = 1.0 / fr;
        }
        else
        {
            duration = prev_frame_delay_;
        }
    }

    double delay = duration;
    double ref_clock = decoder_->get_master_clock();
    double diff = vp->pts - ref_clock;
    double sync_threshold = (delay > 0.1) ? 0.1 : 0.04;

    if (fabs(diff) < 10.0)
    {
        if (diff <= -sync_threshold)
        {
            delay = 0;
        }
        else if (diff >= sync_threshold)
        {
            if (delay > 0.1)
            {
                delay = delay + diff;
            }
            else
            {
                delay = 2 * delay;
            }
        }
    }

    prev_frame_delay_ = delay;

    double time = static_cast<double>(av_gettime_relative()) / 1000000.0;

    if (time < frame_timer_ + delay)
    {
        double remaining_time = frame_timer_ + delay - time;
        int ms = static_cast<int>(remaining_time * 1000);
        if (ms > 0)
        {
            schedule_refresh(ms);
            return;
        }
    }

    frame_timer_ += delay;
    if (delay > 0 && time - frame_timer_ > 0.1)
    {
        frame_timer_ = time;
    }

    int colorspace = 0;
    if (vp->frame->colorspace == AVCOL_SPC_BT709)
    {
        colorspace = 1;
    }

    video_widget_->update_frame(vp->frame, colorspace);
    decoder_->video_frame_queue_.next();

    schedule_refresh(1);
}
