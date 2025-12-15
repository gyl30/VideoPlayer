#include "mainwindow.h"
#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include "log.h"
#include "video_widget.h"
#include "video_decoder.h"

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

void main_window::on_open_action_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Video"), "", tr("Video Files (*.mp4 *.mkv *.avi *.mov *.flv);;All Files (*)"));
    if (fileName.isEmpty())
    {
        return;
    }
    LOG_INFO("selected file {}", fileName.toStdString());

    auto callback = [this](Frame *frame) { this->video_widget_->update_frame(frame->frame); };

    if (!decoder_->open(fileName, callback))
    {
        QMessageBox::critical(this, tr("Error"), tr("Could not open video file!"));
    }
}
