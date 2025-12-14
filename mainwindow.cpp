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
    double remaining_time = 0.01;

retry:
    if (decoder_->video_frame_queue_.remaining() == 0)
    {
        schedule_refresh(10);
        return;
    }

    Frame *lastvp = decoder_->video_frame_queue_.peek_last();
    Frame *vp = decoder_->video_frame_queue_.peek_readable();

    if (vp->serial != decoder_->video_packet_queue_.serial())
    {
        decoder_->video_frame_queue_.next();
        goto retry;
    }

    if (lastvp->serial != vp->serial)
    {
        frame_timer_ = static_cast<double>(av_gettime_relative()) / 1000000.0;
    }

    double duration = vp->duration;
    if (vp->serial == decoder_->video_frame_queue_.peek_next()->serial)
    {
        double next_pts = decoder_->video_frame_queue_.peek_next()->pts;
        if (!std::isnan(next_pts) && !std::isnan(vp->pts))
        {
            duration = next_pts - vp->pts;
        }
    }

    if (std::isnan(duration) || duration <= 0)
    {
        duration = prev_frame_delay_;
    }

    double delay = duration;
    double ref_clock = decoder_->get_master_clock();
    double diff = vp->pts - ref_clock;
    double sync_threshold = (delay > 0.1) ? 0.1 : 0.04;

    if (!std::isnan(diff) && std::abs(diff) < 10.0)
    {
        if (diff <= -sync_threshold)
        {
            delay = std::max(0.0, delay + diff);
        }
        else if (diff >= sync_threshold && delay > 0.1)
        {
            delay = delay + diff;
        }
        else if (diff >= sync_threshold)
        {
            delay = 2 * delay;
        }
    }

    prev_frame_delay_ = delay;
    double time = static_cast<double>(av_gettime_relative()) / 1000000.0;

    if (time < frame_timer_ + delay)
    {
        remaining_time = std::min(frame_timer_ + delay - time, remaining_time);
        schedule_refresh(static_cast<int>(remaining_time * 1000));
        return;
    }

    frame_timer_ += delay;
    if (delay > 0 && time - frame_timer_ > 0.1)
    {
        frame_timer_ = time;
    }

    if (decoder_->video_frame_queue_.remaining() > 1)
    {
        Frame *nextvp = decoder_->video_frame_queue_.peek_next();
        double next_duration = 0.0;
        if (vp->serial == nextvp->serial)
        {
            next_duration = nextvp->pts - vp->pts;
            if (std::isnan(next_duration) || next_duration <= 0)
            {
                next_duration = vp->duration;
            }
        }
        else
        {
            next_duration = vp->duration;
        }

        if (time > frame_timer_ + next_duration)
        {
            decoder_->video_frame_queue_.next();
            goto retry;
        }
    }

    video_widget_->update_frame(vp->frame);
    decoder_->video_frame_queue_.next();

    schedule_refresh(static_cast<int>(remaining_time * 1000));
}
