#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QPixmap>
#include "log.h"
#include "mainwindow.h"
#include "video_decoder.h"

main_window::main_window(QWidget *parent) : QMainWindow(parent)
{
    setup_ui();
    decoder_ = new video_decoder(this);
    connect(decoder_, &video_decoder::frame_ready, this, &main_window::on_frame_ready);
}

main_window::~main_window()
{
    if (decoder_ != nullptr)
    {
        decoder_->stop();
        decoder_->wait();
    }
}

void main_window::setup_ui()
{
    setWindowTitle("VideoPlayer");
    resize(800, 600);

    QMenu *fileMenu = menuBar()->addMenu(tr("File"));
    QAction *openAction = fileMenu->addAction(tr("Open Video"));
    openAction->setShortcut(QKeySequence::Open);

    connect(openAction, &QAction::triggered, this, &main_window::on_open_action_triggered);

    display_ = new QLabel(this);
    display_->setAlignment(Qt::AlignCenter);
    display_->setText(tr("Please open a video file..."));
    setCentralWidget(display_);
}

void main_window::on_open_action_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Video"), "", tr("Video Files (*.mp4 *.mkv *.avi *.mov *.flv);;All Files (*)"));

    if (fileName.isEmpty())
    {
        return;
    }

    LOG_INFO("selected file {}", fileName.toStdString());

    display_->setText("Loading...");

    if (!decoder_->open(fileName))
    {
        QMessageBox::critical(this, tr("Error"), tr("Could not open video file!"));
    }
}

void main_window::on_frame_ready(const QImage &image)
{
    if (image.isNull())
    {
        return;
    }

    QPixmap pixmap = QPixmap::fromImage(image);
    display_->setPixmap(pixmap.scaled(display_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
