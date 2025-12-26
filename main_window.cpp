#include <QDebug>
#include <QStyle>
#include <QTime>
#include "log.h"
#include "main_window.h"

QString format_time(double seconds)
{
    int total_sec = static_cast<int>(seconds);
    if (total_sec < 0)
    {
        total_sec = 0;
    }

    const int h = total_sec / 3600;
    const int m = (total_sec % 3600) / 60;
    const int s = total_sec % 60;

    if (h > 0)
    {
        return QString("%1:%2:%3").arg(h, 2, 10, QChar('0')).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    }
    return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}

main_window::main_window(QWidget *parent) : QMainWindow(parent)
{
    LOG_INFO("main window initializing");
    this->setWindowTitle("Video Player");
    this->resize(900, 600);
    this->setFocusPolicy(Qt::StrongFocus);

    auto *central_widget = new QWidget(this);
    this->setCentralWidget(central_widget);

    auto *main_layout = new QVBoxLayout(central_widget);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    video_widget_ = new video_widget(this);
    video_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    main_layout->addWidget(video_widget_, 1);

    control_panel_ = new QWidget(this);
    control_panel_->setFixedHeight(48);

    auto *control_layout = new QHBoxLayout(control_panel_);
    control_layout->setContentsMargins(10, 5, 10, 5);
    control_layout->setSpacing(8);

    btn_backward_ = new QPushButton("⏮", this);
    btn_backward_->setToolTip("Rewind 15s");

    btn_play_pause_ = new QPushButton("▶", this);
    btn_play_pause_->setToolTip("Play/Pause");

    btn_forward_ = new QPushButton("⏭", this);
    btn_forward_->setToolTip("Forward 15s");

    btn_stop_ = new QPushButton("◼", this);
    btn_stop_->setToolTip("Stop");

    btn_menu_ = new QPushButton("≡", this);
    btn_menu_->setToolTip("Open File");

    control_layout->addWidget(btn_backward_);
    control_layout->addWidget(btn_play_pause_);
    control_layout->addWidget(btn_forward_);

    control_layout->addSpacing(5);
    control_layout->addWidget(btn_stop_);
    control_layout->addWidget(btn_menu_);

    slider_seek_ = new QSlider(Qt::Horizontal, this);
    slider_seek_->setRange(0, 0);
    slider_seek_->setEnabled(false);
    slider_seek_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    control_layout->addWidget(slider_seek_);

    lbl_time_ = new QLabel("00:00", this);
    lbl_time_->setMinimumWidth(50);
    lbl_time_->setAlignment(Qt::AlignCenter);
    control_layout->addWidget(lbl_time_);

    lbl_vol_icon_low_ = new QLabel("🔈", this);

    slider_volume_ = new QSlider(Qt::Horizontal, this);
    slider_volume_->setRange(0, 100);
    slider_volume_->setValue(80);
    slider_volume_->setFixedWidth(80);

    lbl_vol_icon_high_ = new QLabel("🔊", this);

    control_layout->addWidget(lbl_vol_icon_low_);
    control_layout->addWidget(slider_volume_);
    control_layout->addWidget(lbl_vol_icon_high_);

    btn_fullscreen_ = new QPushButton("⛶", this);
    btn_fullscreen_->setToolTip("Fullscreen");
    control_layout->addWidget(btn_fullscreen_);

    main_layout->addWidget(control_panel_);

    init_styles();

    connect(btn_backward_, &QPushButton::clicked, this, &main_window::on_seek_backward);
    connect(btn_play_pause_, &QPushButton::clicked, this, &main_window::on_toggle_pause);
    connect(btn_forward_, &QPushButton::clicked, this, &main_window::on_seek_forward);
    connect(btn_stop_, &QPushButton::clicked, this, &main_window::on_stop_pressed);

    connect(slider_seek_, &QSlider::sliderPressed, this, &main_window::on_slider_pressed);
    connect(slider_seek_, &QSlider::sliderReleased, this, &main_window::on_slider_released);

    connect(btn_menu_, &QPushButton::clicked, this, &main_window::on_open_file);

    connect(slider_volume_, &QSlider::valueChanged, this, &main_window::on_volume_changed);
    connect(btn_fullscreen_, &QPushButton::clicked, this, &main_window::on_toggle_fullscreen);

    ui_timer_ = new QTimer(this);
    ui_timer_->setInterval(200);
    connect(ui_timer_, &QTimer::timeout, this, &main_window::on_update_ui);

    LOG_INFO("main window constructed");
}

void main_window::init_styles() {}

main_window::~main_window()
{
    LOG_INFO("main window destroying");
    stop_play();
}

void main_window::closeEvent(QCloseEvent *event)
{
    LOG_INFO("main window close event triggered");
    stop_play();
    QMainWindow::closeEvent(event);
}

void main_window::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Left)
    {
        LOG_INFO("key left pressed");
        do_seek_relative(-15.0);
    }
    else if (event->key() == Qt::Key_Right)
    {
        LOG_INFO("key right pressed");
        do_seek_relative(15.0);
    }
    else if (event->key() == Qt::Key_Space)
    {
        LOG_INFO("key space pressed");
        on_toggle_pause();
    }
    else if (event->key() == Qt::Key_Escape && isFullScreen())
    {
        LOG_INFO("key escape pressed in fullscreen");
        on_toggle_fullscreen();
    }
    else
    {
        QMainWindow::keyPressEvent(event);
    }
}

void main_window::on_open_file()
{
    LOG_INFO("on open file clicked");
    const QString filename = QFileDialog::getOpenFileName(this, "Open Video", "", "Video Files (*.mp4 *.mkv *.avi *.mov *.flv)");
    if (filename.isEmpty())
    {
        LOG_INFO("open file cancelled");
        return;
    }

    LOG_INFO("open file selected {}", filename.toStdString());
    stop_play();
    if (!start_play(filename.toStdString()))
    {
        LOG_ERROR("failed to start play for {}", filename.toStdString());
        QMessageBox::critical(this, "Error", "Failed to open video file");
    }
}

void main_window::on_toggle_pause()
{
    if (!playing_)
    {
        return;
    }
    paused_ = !paused_;

    LOG_INFO("toggle pause state new state paused {}", paused_);

    btn_play_pause_->setText(paused_ ? "▶" : "⏸");

    if (audio_backend_ != nullptr)
    {
        audio_backend_->pause(paused_);
    }
    if (clock_ != nullptr)
    {
        paused_ ? clock_->pause() : clock_->resume();
    }
    if (sync_thread_ != nullptr)
    {
        sync_thread_->paused(paused_);
    }
}

void main_window::on_stop_pressed()
{
    LOG_INFO("stop pressed");
    stop_play();

    slider_seek_->setValue(0);
    slider_seek_->setEnabled(false);
    lbl_time_->setText("00:00");
    btn_play_pause_->setText("▶");
}

void main_window::on_toggle_fullscreen()
{
    LOG_INFO("toggle fullscreen");
    if (isFullScreen())
    {
        showNormal();
        control_panel_->show();
    }
    else
    {
        showFullScreen();
    }
}

void main_window::on_volume_changed(int value)
{
    if (audio_backend_ != nullptr)
    {
        audio_backend_->set_volume(value);
    }
}

void main_window::on_seek_forward()
{
    LOG_INFO("seek forward clicked");
    do_seek_relative(15.0);
}

void main_window::on_seek_backward()
{
    LOG_INFO("seek backward clicked");
    do_seek_relative(-15.0);
}

void main_window::do_seek_relative(double seconds)
{
    if (demuxer_ == nullptr || clock_ == nullptr)
    {
        return;
    }

    const double current = clock_->get();
    double target = current + seconds;

    if (target < 0.0)
    {
        target = 0.0;
    }
    if (target > duration_)
    {
        target = duration_ - 1.0;
    }

    LOG_INFO("seeking relative current {} target {}", current, target);

    if (video_frame_queue_ != nullptr)
    {
        video_frame_queue_->clear();
    }
    if (audio_frame_queue_ != nullptr)
    {
        audio_frame_queue_->clear();
    }

    demuxer_->seek(target);
}

void main_window::on_slider_pressed()
{
    LOG_INFO("slider pressed");
    ui_timer_->stop();
}

void main_window::on_slider_released()
{
    if (demuxer_ != nullptr)
    {
        const auto val = static_cast<double>(slider_seek_->value());
        LOG_INFO("slider released seeking to {}", val);

        if (video_frame_queue_ != nullptr)
        {
            video_frame_queue_->clear();
        }
        if (audio_frame_queue_ != nullptr)
        {
            audio_frame_queue_->clear();
        }

        demuxer_->seek(val);
        ui_timer_->start();
    }
}

void main_window::on_update_ui()
{
    if (!playing_ || clock_ == nullptr)
    {
        return;
    }

    const double current = clock_->get();

    if (!slider_seek_->isSliderDown())
    {
        slider_seek_->setValue(static_cast<int>(current));
    }

    lbl_time_->setText(format_time(current));
}

void main_window::stop_play()
{
    if (!playing_)
    {
        return;
    }
    LOG_INFO("stopping play");

    playing_ = false;
    paused_ = false;
    ui_timer_->stop();

    LOG_INFO("aborting queues");
    if (video_pkt_queue_ != nullptr)
    {
        video_pkt_queue_->abort();
    }
    if (audio_pkt_queue_ != nullptr)
    {
        audio_pkt_queue_->abort();
    }
    if (video_frame_queue_ != nullptr)
    {
        video_frame_queue_->abort();
    }
    if (audio_frame_queue_ != nullptr)
    {
        audio_frame_queue_->abort();
    }
    if (demuxer_ != nullptr)
    {
        LOG_INFO("stopping demuxer");
        demuxer_->stop();
    }

    LOG_INFO("stopping sync thread");
    if (sync_thread_ != nullptr)
    {
        sync_thread_->stop();
        sync_thread_->wait();
        sync_thread_.reset();
    }

    LOG_INFO("joining threads");
    if (demux_thread_.joinable())
    {
        demux_thread_.join();
    }
    if (video_decoder_thread_.joinable())
    {
        video_decoder_thread_.join();
    }
    if (audio_decoder_thread_.joinable())
    {
        audio_decoder_thread_.join();
    }

    LOG_INFO("closing audio backend");
    if (audio_backend_ != nullptr)
    {
        audio_backend_->close();
        audio_backend_.reset();
    }

    demuxer_.reset();
    video_decoder_.reset();
    audio_decoder_.reset();
    clock_.reset();

    video_pkt_queue_.reset();
    audio_pkt_queue_.reset();
    video_frame_queue_.reset();
    audio_frame_queue_.reset();

    if (video_widget_ != nullptr)
    {
        video_widget_->clear();
    }
    LOG_INFO("stop play finished");
}

bool main_window::start_play(const std::string &filepath)
{
    LOG_INFO("starting play for file {}", filepath);
    video_pkt_queue_ = std::make_unique<safe_queue<std::shared_ptr<media_packet>>>(100);
    audio_pkt_queue_ = std::make_unique<safe_queue<std::shared_ptr<media_packet>>>(100);
    video_frame_queue_ = std::make_unique<safe_queue<std::shared_ptr<media_frame>>>(16);
    audio_frame_queue_ = std::make_unique<safe_queue<std::shared_ptr<media_frame>>>(64);

    clock_ = std::make_unique<av_clock>();

    demuxer_ = std::make_unique<demuxer>();
    if (!demuxer_->open(filepath, video_pkt_queue_.get(), audio_pkt_queue_.get()))
    {
        LOG_ERROR("failed to open demuxer");
        return false;
    }
    LOG_INFO("demuxer opened");

    demuxer_->set_seek_cb(
        [this](double time)
        {
            QMetaObject::invokeMethod(this,
                                      [this, time]()
                                      {
                                          LOG_INFO("UI received seek finish callback time {}", time);
                                          if (!slider_seek_->isSliderDown())
                                          {
                                              slider_seek_->setValue(static_cast<int>(time));
                                              lbl_time_->setText(format_time(time));
                                          }
                                      });
        });

    duration_ = demuxer_->duration();
    slider_seek_->setRange(0, static_cast<int>(duration_));
    slider_seek_->setEnabled(true);

    video_decoder_ = std::make_unique<decoder>();
    audio_decoder_ = std::make_unique<decoder>();

    if (demuxer_->video_index() >= 0)
    {
        LOG_INFO("video stream found index {}", demuxer_->video_index());
        if (!video_decoder_->open(demuxer_->codec_par(demuxer_->video_index()), video_pkt_queue_.get(), video_frame_queue_.get(), "Video"))
        {
            LOG_ERROR("failed to open video decoder");
            return false;
        }
    }

    if (demuxer_->audio_index() >= 0)
    {
        LOG_INFO("audio stream found index {}", demuxer_->audio_index());
        if (!audio_decoder_->open(demuxer_->codec_par(demuxer_->audio_index()), audio_pkt_queue_.get(), audio_frame_queue_.get(), "Audio"))
        {
            LOG_ERROR("failed to open audio decoder");
            return false;
        }
    }

    if (demuxer_->audio_index() >= 0)
    {
        audio_backend_ = std::make_unique<sdl_audio_backend>();
        if (!audio_backend_->init(audio_frame_queue_.get(), audio_pkt_queue_.get(), demuxer_->time_base(demuxer_->audio_index()), clock_.get()))
        {
            LOG_ERROR("failed to init audio backend");
            return false;
        }
        audio_backend_->set_volume(slider_volume_->value());
    }

    if (demuxer_->video_index() >= 0)
    {
        sync_thread_ = std::make_unique<video_sync_thread>(
            video_frame_queue_.get(), video_pkt_queue_.get(), demuxer_->time_base(demuxer_->video_index()), clock_.get());

        connect(sync_thread_.get(), &video_sync_thread::frame_ready, video_widget_, &video_widget::on_frame_ready);

        sync_thread_->start();
    }

    LOG_INFO("starting threads");
    demux_thread_ = std::thread([this]() { demuxer_->run(); });

    video_decoder_thread_ = std::thread(
        [this]()
        {
            if (demuxer_->video_index() >= 0)
            {
                video_decoder_->run();
            }
        });

    audio_decoder_thread_ = std::thread(
        [this]()
        {
            if (demuxer_->audio_index() >= 0)
            {
                audio_decoder_->run();
            }
        });

    playing_ = true;
    paused_ = false;
    btn_play_pause_->setText("⏸");
    ui_timer_->start();
    this->setFocus();

    LOG_INFO("play started successfully");

    return true;
}
