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
    decoder_ = new video_decoder(this);
    setup_ui();

    update_timer_ = new QTimer(this);
    update_timer_->setInterval(500);
    connect(update_timer_, &QTimer::timeout, this, &main_window::on_timer_timeout);

    connect(decoder_, &video_decoder::media_info_loaded, this, &main_window::on_media_info_loaded, Qt::QueuedConnection);
    connect(decoder_, &video_decoder::seek_finished, this, &main_window::on_seek_finished, Qt::QueuedConnection);
    connect(decoder_, &video_decoder::error_occurred, this, &main_window::on_decoder_error, Qt::QueuedConnection);
}

main_window::~main_window()
{
    if (decoder_ != nullptr)
    {
        decoder_->stop(get_next_op_id());
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

    central_widget_ = new QWidget(this);
    setCentralWidget(central_widget_);

    main_layout_ = new QVBoxLayout(central_widget_);
    main_layout_->setContentsMargins(0, 0, 0, 0);
    main_layout_->setSpacing(0);

    video_widget_ = new video_widget(this);
    video_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    main_layout_->addWidget(video_widget_);

    QWidget *control_panel = new QWidget(this);
    control_panel->setFixedHeight(60);
    control_panel->setStyleSheet("background-color: #333; color: white;");

    control_layout_ = new QHBoxLayout(control_panel);

    current_time_lbl_ = new QLabel("00:00", this);
    current_time_lbl_->setFixedWidth(60);
    current_time_lbl_->setAlignment(Qt::AlignCenter);

    seek_slider_ = new QSlider(Qt::Horizontal, this);
    seek_slider_->setRange(0, 1000);
    seek_slider_->setEnabled(false);
    seek_slider_->setStyleSheet(
        "QSlider::groove:horizontal { border: 1px solid #999; height: 8px; background: #555; margin: 2px 0; border-radius: 4px; }"
        "QSlider::handle:horizontal { background: #3a8ee6; border: 1px solid #5c5c5c; width: 18px; height: 18px; margin: -7px 0; border-radius: 9px; "
        "}"
        "QSlider::sub-page:horizontal { background: #3a8ee6; border-radius: 4px; }");

    connect(seek_slider_, &QSlider::sliderPressed, this, &main_window::on_slider_pressed);
    connect(seek_slider_, &QSlider::sliderReleased, this, &main_window::on_slider_released);
    connect(seek_slider_, &QSlider::valueChanged, this, &main_window::on_slider_moved);

    total_time_lbl_ = new QLabel("00:00", this);
    total_time_lbl_->setFixedWidth(60);
    total_time_lbl_->setAlignment(Qt::AlignCenter);

    control_layout_->addWidget(current_time_lbl_);
    control_layout_->addWidget(seek_slider_);
    control_layout_->addWidget(total_time_lbl_);

    main_layout_->addWidget(control_panel);
}

int64_t main_window::get_next_op_id() { return ++current_op_id_; }

void main_window::on_open_action_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Video"), "", tr("Video Files (*.mp4 *.mkv *.avi *.mov *.flv);;All Files (*)"));
    if (fileName.isEmpty())
    {
        return;
    }

    int64_t op_id = get_next_op_id();
    LOG_INFO("op id {} user action open file {}", op_id, fileName.toStdString());

    auto callback = [this](Frame *frame) { this->video_widget_->update_frame(frame->frame); };
    decoder_->set_render_callback(callback);

    current_time_lbl_->setText("Loading...");
    decoder_->open_async(fileName, op_id);
}

void main_window::on_media_info_loaded(int64_t op_id, double duration)
{
    LOG_INFO("op id {} ui received media info duration {:.2f}", op_id, duration);

    seek_slider_->setValue(0);
    seek_slider_->setEnabled(true);
    total_time_lbl_->setText(format_time(duration));
    update_timer_->start();

    LOG_INFO("op id {} ui commanding decoder start", op_id);
    decoder_->start(op_id);
}

void main_window::on_slider_pressed() { is_seeking_ = true; }

void main_window::on_slider_released()
{
    if (decoder_ == nullptr)
    {
        return;
    }

    int64_t op_id = get_next_op_id();
    double duration = decoder_->get_duration();
    int value = seek_slider_->value();
    double target_pos = (static_cast<double>(value) / 1000.0) * duration;

    LOG_INFO("op id {} user action seek to {:.2f}", op_id, target_pos);
    decoder_->seek_async(target_pos, op_id);
}

void main_window::on_slider_moved(int value)
{
    if (is_seeking_ && decoder_)
    {
        double duration = decoder_->get_duration();
        if (duration > 0)
        {
            double target_pos = (static_cast<double>(value) / 1000.0) * duration;
            current_time_lbl_->setText(format_time(target_pos));
        }
    }
}

void main_window::on_seek_finished(int64_t op_id, double target_pos, bool success)
{
    if (!success)
    {
        LOG_ERROR("op id {} ui received seek failed", op_id);
        is_seeking_ = false;
        return;
    }

    LOG_INFO("op id {} ui received seek success updating ui", op_id);

    LOG_INFO("op id {} ui commanding decoder resume", op_id);
    decoder_->start(op_id);
    is_seeking_ = false;
}

void main_window::on_decoder_error(int64_t op_id, QString msg)
{
    LOG_ERROR("op id {} error occurred {}", op_id, msg.toStdString());
    QMessageBox::warning(this, "Error", msg);
}

void main_window::on_timer_timeout()
{
    if (is_seeking_ || decoder_ == nullptr)
    {
        return;
    }

    double duration = decoder_->get_duration();
    if (duration <= 0)
    {
        return;
    }

    double current = decoder_->get_master_clock();
    if (std::isnan(current))
    {
        return;
    }

    int slider_val = static_cast<int>((current / duration) * 1000.0);

    if (abs(seek_slider_->value() - slider_val) > 1)
    {
        seek_slider_->blockSignals(true);
        seek_slider_->setValue(slider_val);
        seek_slider_->blockSignals(false);
    }

    current_time_lbl_->setText(format_time(current));
}

QString main_window::format_time(double seconds)
{
    if (std::isnan(seconds) || seconds < 0)
    {
        seconds = 0;
    }
    int total_sec = static_cast<int>(seconds);
    int h = total_sec / 3600;
    int m = (total_sec % 3600) / 60;
    int s = total_sec % 60;

    if (h > 0)
    {
        return QString::asprintf("%02d:%02d:%02d", h, m, s);
    }

    return QString::asprintf("%02d:%02d", m, s);
}
