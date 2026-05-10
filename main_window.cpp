#include <QDebug>
#include <QAbstractItemView>
#include <QAbstractButton>
#include <QFileInfo>
#include <QFontMetrics>
#include <QIcon>
#include <QKeySequence>
#include <QMouseEvent>
#include <QTime>
#include <QToolTip>
#include <QWindow>
#include "log.h"
#include "main_window.h"
#include "volumemeter.h"

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

    return QString("%1:%2:%3").arg(h, 2, 10, QChar('0')).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}

main_window::main_window(QWidget *parent) : QMainWindow(parent)
{
    LOG_INFO("main window initializing");
    this->setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    this->setWindowTitle("视频播放器");
    this->resize(1080, 680);
    this->setMinimumSize(760, 480);
    this->setFocusPolicy(Qt::StrongFocus);

    auto *central_widget = new QWidget(this);
    central_widget->setObjectName("rootWidget");
    this->setCentralWidget(central_widget);

    auto *main_layout = new QVBoxLayout(central_widget);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    title_bar_ = new QWidget(this);
    title_bar_->setObjectName("titleBar");
    title_bar_->setFixedHeight(48);
    title_bar_->installEventFilter(this);

    auto *title_layout = new QHBoxLayout(title_bar_);
    title_layout->setContentsMargins(0, 0, 0, 0);
    title_layout->setSpacing(0);

    title_drag_area_ = new QWidget(title_bar_);
    title_drag_area_->setObjectName("titleDragArea");
    title_drag_area_->installEventFilter(this);
    title_drag_area_->setCursor(Qt::ArrowCursor);

    auto *title_drag_layout = new QHBoxLayout(title_drag_area_);
    title_drag_layout->setContentsMargins(16, 0, 0, 0);
    title_drag_layout->setSpacing(10);

    auto *app_badge = new QLabel(this);
    app_badge->setObjectName("appBadge");
    app_badge->setAlignment(Qt::AlignCenter);
    app_badge->setFixedSize(30, 30);
    app_badge->setAttribute(Qt::WA_TransparentForMouseEvents);
    app_badge->setPixmap(QIcon(":/icons/app_icon.svg").pixmap(26, 26));
    title_drag_layout->addWidget(app_badge);

    auto *app_title = new QLabel("视频播放器", this);
    app_title->setObjectName("appTitle");
    app_title->setAttribute(Qt::WA_TransparentForMouseEvents);
    title_drag_layout->addWidget(app_title);

    title_drag_layout->addStretch(1);

    lbl_media_title_ = new QLabel("视频播放器", this);
    lbl_media_title_->setObjectName("mediaTitle");
    lbl_media_title_->setAlignment(Qt::AlignCenter);
    lbl_media_title_->setMinimumWidth(260);
    lbl_media_title_->setMaximumWidth(460);
    lbl_media_title_->setAttribute(Qt::WA_TransparentForMouseEvents);
    title_drag_layout->addWidget(lbl_media_title_);

    title_drag_layout->addStretch(1);
    title_layout->addWidget(title_drag_area_, 1);

    auto *title_button_box = new QWidget(title_bar_);
    title_button_box->setObjectName("titleButtonBox");
    title_button_box->setFixedWidth(142);

    auto *title_button_layout = new QHBoxLayout(title_button_box);
    title_button_layout->setContentsMargins(0, 0, 0, 0);
    title_button_layout->setSpacing(0);

    btn_title_minimize_ = new QPushButton("−", this);
    btn_title_minimize_->setObjectName("windowButton");
    btn_title_minimize_->setCursor(Qt::PointingHandCursor);
    btn_title_minimize_->setToolTip("最小化");

    btn_title_maximize_ = new QPushButton("□", this);
    btn_title_maximize_->setObjectName("windowButton");
    btn_title_maximize_->setCursor(Qt::PointingHandCursor);
    btn_title_maximize_->setToolTip("最大化");

    btn_title_close_ = new QPushButton("×", this);
    btn_title_close_->setObjectName("closeButton");
    btn_title_close_->setCursor(Qt::PointingHandCursor);
    btn_title_close_->setToolTip("关闭");

    title_button_layout->addWidget(btn_title_minimize_);
    title_button_layout->addWidget(btn_title_maximize_);
    title_button_layout->addWidget(btn_title_close_);
    title_layout->addWidget(title_button_box);

    main_layout->addWidget(title_bar_);

    auto *content_widget = new QWidget(this);
    content_widget->setObjectName("contentWidget");
    auto *content_layout = new QHBoxLayout(content_widget);
    content_layout->setContentsMargins(0, 0, 0, 0);
    content_layout->setSpacing(0);

    video_frame_ = new QFrame(this);
    video_frame_->setObjectName("videoFrame");
    auto *video_layout = new QVBoxLayout(video_frame_);
    video_layout->setContentsMargins(0, 0, 0, 0);
    video_layout->setSpacing(0);

    video_widget_ = new video_widget(video_frame_);
    video_widget_->setObjectName("videoSurface");
    video_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    video_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
    video_layout->addWidget(video_widget_, 1);
    content_layout->addWidget(video_frame_, 1);

    playlist_panel_ = new QFrame(this);
    playlist_panel_->setObjectName("playlistPanel");
    playlist_panel_->setFixedWidth(238);

    auto *playlist_layout = new QVBoxLayout(playlist_panel_);
    playlist_layout->setContentsMargins(14, 14, 14, 14);
    playlist_layout->setSpacing(10);

    auto *playlist_header_layout = new QHBoxLayout();
    playlist_header_layout->setContentsMargins(0, 0, 0, 0);
    playlist_header_layout->setSpacing(8);

    auto *playlist_title = new QLabel("播放列表", this);
    playlist_title->setObjectName("playlistTitle");
    lbl_playlist_count_ = new QLabel("0 个文件", this);
    lbl_playlist_count_->setObjectName("playlistCount");
    lbl_playlist_count_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    playlist_header_layout->addWidget(playlist_title);
    playlist_header_layout->addStretch(1);
    playlist_header_layout->addWidget(lbl_playlist_count_);
    playlist_layout->addLayout(playlist_header_layout);

    playlist_view_ = new QListWidget(this);
    playlist_view_->setObjectName("playlistView");
    playlist_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    playlist_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    playlist_view_->addItem("打开文件后显示在这里");
    playlist_layout->addWidget(playlist_view_, 1);
    playlist_panel_->hide();
    content_layout->addWidget(playlist_panel_);

    main_layout->addWidget(content_widget, 1);

    control_panel_ = new QWidget(this);
    control_panel_->setObjectName("controlPanel");
    control_panel_->setFixedHeight(124);

    auto *control_layout = new QVBoxLayout(control_panel_);
    control_layout->setContentsMargins(0, 0, 0, 0);
    control_layout->setSpacing(0);

    auto *seek_row = new QWidget(this);
    seek_row->setObjectName("seekRow");
    seek_row->setFixedHeight(28);

    auto *seek_layout = new QHBoxLayout(seek_row);
    seek_layout->setContentsMargins(10, 0, 10, 0);
    seek_layout->setSpacing(0);

    auto *btn_seek_back = new QPushButton(QIcon(":/icons/skip-backward-fill.svg"), QString(), this);
    btn_seek_back->setObjectName("seekEdgeButton");
    btn_seek_back->setCursor(Qt::PointingHandCursor);
    btn_seek_back->setIconSize(QSize(14, 14));
    btn_seek_back->setToolTip("快退 15 秒");

    slider_seek_ = new QSlider(Qt::Horizontal, this);
    slider_seek_->setObjectName("seekSlider");
    slider_seek_->setRange(0, 0);
    slider_seek_->setEnabled(false);
    slider_seek_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    slider_seek_->setFixedHeight(18);

    auto *btn_seek_forward = new QPushButton(QIcon(":/icons/skip-forward-fill.svg"), QString(), this);
    btn_seek_forward->setObjectName("seekEdgeButton");
    btn_seek_forward->setCursor(Qt::PointingHandCursor);
    btn_seek_forward->setIconSize(QSize(14, 14));
    btn_seek_forward->setToolTip("快进 15 秒");

    seek_layout->addWidget(btn_seek_back);
    seek_layout->addWidget(slider_seek_, 1);
    seek_layout->addWidget(btn_seek_forward);
    control_layout->addWidget(seek_row);

    auto *control_bar = new QWidget(this);
    control_bar->setObjectName("controlBar");

    auto *control_row = new QHBoxLayout(control_bar);
    control_row->setContentsMargins(20, 8, 20, 10);
    control_row->setSpacing(10);

    lbl_time_ = new QLabel("00:00:00 / 00:00:00", this);
    lbl_time_->setObjectName("timeLabel");
    lbl_time_->setMinimumWidth(210);
    lbl_time_->setAlignment(Qt::AlignCenter);

    btn_stop_ = new QPushButton(QIcon(":/icons/stop.svg"), QString(), this);
    btn_stop_->setObjectName("controlButton");
    btn_stop_->setCursor(Qt::PointingHandCursor);
    btn_stop_->setIconSize(QSize(18, 18));
    btn_stop_->setToolTip("停止");

    btn_backward_ = new QPushButton(QIcon(":/icons/previous.svg"), QString(), this);
    btn_backward_->setObjectName("controlButton");
    btn_backward_->setCursor(Qt::PointingHandCursor);
    btn_backward_->setIconSize(QSize(18, 18));
    btn_backward_->setToolTip("播放上一个");

    btn_play_pause_ = new QPushButton(QIcon(":/icons/play.svg"), QString(), this);
    btn_play_pause_->setObjectName("controlButton");
    btn_play_pause_->setCursor(Qt::PointingHandCursor);
    btn_play_pause_->setIconSize(QSize(18, 18));
    btn_play_pause_->setToolTip("播放/暂停");

    btn_forward_ = new QPushButton(QIcon(":/icons/next.svg"), QString(), this);
    btn_forward_->setObjectName("controlButton");
    btn_forward_->setCursor(Qt::PointingHandCursor);
    btn_forward_->setIconSize(QSize(18, 18));
    btn_forward_->setToolTip("播放下一个");

    lbl_vol_icon_low_ = new QLabel(this);
    lbl_vol_icon_low_->setObjectName("volumeIcon");
    lbl_vol_icon_low_->setAlignment(Qt::AlignCenter);
    lbl_vol_icon_low_->setFixedSize(20, 20);
    lbl_vol_icon_low_->setAttribute(Qt::WA_TransparentForMouseEvents);

    volume_meter_ = new volume_meter(this);
    volume_meter_->setObjectName("volumeMeter");
    volume_meter_->setRange(0, 100);
    volume_meter_->setValue(80);
    volume_meter_->setFixedSize(68, 10);
    volume_meter_->setOrientation(Qt::Horizontal);
    volume_meter_->setToolTip("音量");

    btn_playlist_ = new QPushButton(QIcon(":/icons/playlist.svg"), QString(), this);
    btn_playlist_->setObjectName("toolBlockButton");
    btn_playlist_->setCursor(Qt::PointingHandCursor);
    btn_playlist_->setIconSize(QSize(18, 18));
    btn_playlist_->setCheckable(true);
    btn_playlist_->setChecked(false);
    btn_playlist_->setToolTip("显示/隐藏播放列表");

    control_row->addWidget(lbl_time_);
    control_row->addStretch(1);
    control_row->addWidget(btn_stop_);
    control_row->addWidget(btn_backward_);
    control_row->addWidget(btn_play_pause_);
    control_row->addWidget(btn_forward_);
    control_row->addStretch(1);
    control_row->addWidget(lbl_vol_icon_low_);
    control_row->addWidget(volume_meter_);
    control_row->addSpacing(12);
    control_row->addWidget(btn_playlist_);

    control_layout->addWidget(control_bar, 1);

    main_layout->addWidget(control_panel_);

    init_styles();

    connect(btn_backward_, &QPushButton::clicked, this, &main_window::on_play_previous);
    connect(btn_seek_back, &QPushButton::clicked, this, &main_window::on_seek_backward);
    connect(btn_play_pause_, &QPushButton::clicked, this, &main_window::on_toggle_pause);
    connect(btn_forward_, &QPushButton::clicked, this, &main_window::on_play_next);
    connect(btn_seek_forward, &QPushButton::clicked, this, &main_window::on_seek_forward);
    connect(btn_stop_, &QPushButton::clicked, this, &main_window::on_stop_pressed);

    connect(slider_seek_, &QSlider::sliderPressed, this, &main_window::on_slider_pressed);
    connect(slider_seek_, &QSlider::sliderReleased, this, &main_window::on_slider_released);

    connect(btn_playlist_, &QPushButton::clicked, this, &main_window::on_toggle_playlist);
    connect(playlist_view_, &QListWidget::itemDoubleClicked, this, &main_window::on_playlist_item_activated);
    connect(playlist_view_, &QListWidget::currentRowChanged, this, [this](int) { update_playlist_buttons(); });
    connect(video_widget_, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos)
            {
                QMenu menu(this);
                QAction *open_action = menu.addAction("打开");
                QAction *fullscreen_action = menu.addAction(isFullScreen() ? "退出全屏" : "全屏");
                QAction *chosen = menu.exec(video_widget_->mapToGlobal(pos));
                if (chosen == open_action)
                {
                    on_open_file();
                }
                else if (chosen == fullscreen_action)
                {
                    on_toggle_fullscreen();
                }
            });

    connect(volume_meter_, &volume_meter::value_changed, this, &main_window::on_volume_changed);
    connect(btn_title_minimize_, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(btn_title_maximize_, &QPushButton::clicked, this, &main_window::toggle_window_maximized);
    connect(btn_title_close_, &QPushButton::clicked, this, &QWidget::close);

    ui_timer_ = new QTimer(this);
    ui_timer_->setInterval(200);
    connect(ui_timer_, &QTimer::timeout, this, &main_window::on_update_ui);

    title_scroll_timer_ = new QTimer(this);
    title_scroll_timer_->setInterval(180);
    connect(title_scroll_timer_, &QTimer::timeout, this, &main_window::on_title_scroll_tick);

    set_media_title_text("视频播放器");
    update_volume_icon(volume_meter_->value());
    update_playlist_buttons();

    for (auto *button : findChildren<QAbstractButton *>())
    {
        if (!button->toolTip().isEmpty())
        {
            button->setToolTipDuration(3000);
            button->installEventFilter(this);
        }
    }

    LOG_INFO("main window constructed");
}

void main_window::init_styles()
{
    this->setStyleSheet(
        "QMainWindow, QWidget#rootWidget {"
        "    background: #05080c;"
        "    color: #d8e0ea;"
        "    font-family: \"Microsoft YaHei\", \"Segoe UI\", sans-serif;"
        "    font-size: 13px;"
        "}"
        "QWidget#titleBar {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1c6097, stop:0.48 #143d65, stop:1 #0a2036);"
        "    border-bottom: 1px solid #06111c;"
        "}"
        "QWidget#titleDragArea {"
        "    background: transparent;"
        "}"
        "QWidget#titleButtonBox {"
        "    background: transparent;"
        "}"
        "QLabel#appBadge {"
        "    background: transparent;"
        "}"
        "QLabel#appTitle {"
        "    color: #f2f7fb;"
        "    font-size: 20px;"
        "    font-weight: 700;"
        "}"
        "QLabel#mediaTitle {"
        "    color: #ffffff;"
        "    font-size: 20px;"
        "    font-weight: 700;"
        "    padding-left: 6px;"
        "    padding-right: 6px;"
        "}"
        "QLabel#playlistCount {"
        "    color: #8fa4b9;"
        "    font-size: 12px;"
        "}"
        "QPushButton#titleToolButton, QPushButton#windowButton {"
        "    background: transparent;"
        "    border: none;"
        "    color: #ecf8ff;"
        "    font-size: 23px;"
        "    font-weight: 700;"
        "    min-width: 44px;"
        "    max-width: 44px;"
        "    min-height: 48px;"
        "    max-height: 48px;"
        "}"
        "QPushButton#titleToolButton:hover, QPushButton#windowButton:hover {"
        "    background: rgba(255, 255, 255, 0.12);"
        "}"
        "QPushButton#titleToolButton:pressed, QPushButton#windowButton:pressed {"
        "    background: rgba(0, 0, 0, 0.2);"
        "}"
        "QPushButton#closeButton {"
        "    background: #8c201c;"
        "    border: none;"
        "    color: #ffffff;"
        "    font-size: 32px;"
        "    font-weight: 700;"
        "    min-width: 54px;"
        "    max-width: 54px;"
        "    min-height: 48px;"
        "    max-height: 48px;"
        "}"
        "QPushButton#closeButton:hover {"
        "    background: #c62a24;"
        "}"
        "QPushButton#closeButton:pressed {"
        "    background: #641714;"
        "}"
        "QWidget#contentWidget {"
        "    background: #000000;"
        "}"
        "QFrame#videoFrame {"
        "    background: #000000;"
        "    border: none;"
        "}"
        "QOpenGLWidget#videoSurface {"
        "    background: #000000;"
        "}"
        "QFrame#playlistPanel {"
        "    background: #0b1929;"
        "    border-left: 1px solid #16385d;"
        "}"
        "QLabel#playlistTitle {"
        "    color: #eef4fa;"
        "    font-size: 14px;"
        "    font-weight: 600;"
        "}"
        "QListWidget#playlistView {"
        "    background: transparent;"
        "    border: none;"
        "    color: #b8c4cf;"
        "    outline: none;"
        "}"
        "QListWidget#playlistView::item {"
        "    height: 34px;"
        "    padding-left: 10px;"
        "    padding-right: 10px;"
        "    border-radius: 4px;"
        "}"
        "QListWidget#playlistView::item:hover {"
        "    background: #1b2631;"
        "    color: #f3f8fc;"
        "}"
        "QListWidget#playlistView::item:selected {"
        "    background: #174a68;"
        "    color: #ffffff;"
        "}"
        "QWidget#controlPanel {"
        "    background: #071b30;"
        "    border-top: 1px solid #2c74ac;"
        "}"
        "QWidget#seekRow {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #020406, stop:1 #07101a);"
        "    border: none;"
        "}"
        "QWidget#controlBar {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1c6097, stop:0.28 #174d7e, stop:1 #0b2138);"
        "}"
        "QPushButton#seekEdgeButton {"
        "    background: transparent;"
        "    border: none;"
        "    color: #eef8ff;"
        "    font-size: 18px;"
        "    font-weight: 700;"
        "    min-width: 28px;"
        "    max-width: 28px;"
        "    min-height: 24px;"
        "    max-height: 24px;"
        "}"
        "QPushButton#seekEdgeButton:hover {"
        "    color: #ffffff;"
        "    background: rgba(255, 255, 255, 0.08);"
        "}"
        "QPushButton#controlButton, QPushButton#controlButtonWide {"
        "    background: transparent;"
        "    color: #f5fbff;"
        "    border: none;"
        "    border-radius: 2px;"
        "    min-height: 54px;"
        "}"
        "QPushButton#controlButton {"
        "    min-width: 44px;"
        "    max-width: 44px;"
        "    font-size: 28px;"
        "    font-weight: 700;"
        "}"
        "QPushButton#controlButtonWide {"
        "    min-width: 62px;"
        "    padding-left: 10px;"
        "    padding-right: 10px;"
        "}"
        "QPushButton#controlButton:hover, QPushButton#controlButtonWide:hover {"
        "    background: rgba(255, 255, 255, 0.12);"
        "    color: #ffffff;"
        "}"
        "QPushButton#controlButton:pressed, QPushButton#controlButtonWide:pressed {"
        "    background: rgba(8, 29, 49, 0.9);"
        "}"
        "QPushButton#controlButtonWide:checked {"
        "    background: #1e7dbd;"
        "    border-color: #83d7ff;"
        "    color: #ffffff;"
        "}"
        "QLabel#timeLabel {"
        "    color: #eefaff;"
        "    font-family: Consolas, \"Microsoft YaHei\", monospace;"
        "    font-size: 22px;"
        "    font-weight: 500;"
        "}"
        "QLabel#volumeIcon {"
        "    min-width: 20px;"
        "    max-width: 20px;"
        "}"
        "QPushButton#toolBlockButton {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(40, 89, 132, 0.8), stop:1 rgba(16, 42, 69, 0.95));"
        "    border: 1px solid rgba(106, 178, 229, 0.36);"
        "    color: #effbff;"
        "    border-radius: 1px;"
        "    min-width: 56px;"
        "    max-width: 56px;"
        "    min-height: 58px;"
        "    max-height: 58px;"
        "    font-size: 18px;"
        "}"
        "QPushButton#toolBlockButton:hover {"
        "    background: rgba(45, 126, 187, 0.78);"
        "    border-color: #86d7ff;"
        "}"
        "QPushButton#toolBlockButton:checked {"
        "    background: #1e7dbd;"
        "    border-color: #9ce3ff;"
        "}"
        "QSlider::groove:horizontal {"
        "    height: 6px;"
        "    background: #0c2741;"
        "    border-radius: 2px;"
        "}"
        "QSlider::sub-page:horizontal {"
        "    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4f6cf3, stop:0.78 #97b3ff, stop:1 #f7fdff);"
        "    border-radius: 1px;"
        "}"
        "QSlider::add-page:horizontal {"
        "    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #152235, stop:1 #08101a);"
        "    border-radius: 1px;"
        "}"
        "QSlider::handle:horizontal {"
        "    width: 18px;"
        "    height: 18px;"
        "    margin: -7px 0;"
        "    background: #edf7ff;"
        "    border: 2px solid #96dfff;"
        "    border-radius: 9px;"
        "}"
        "QSlider::handle:horizontal:hover {"
        "    background: #ffffff;"
        "}"
        "QSlider#seekSlider {"
        "    background: transparent;"
        "    min-height: 16px;"
        "    max-height: 16px;"
        "}"
        "QSlider#seekSlider::groove:horizontal {"
        "    background: rgba(7, 83, 119, 165);"
        "    border: none;"
        "    height: 6px;"
        "    border-radius: 3px;"
        "}"
        "QSlider#seekSlider::sub-page:horizontal {"
        "    background: #B9FF95;"
        "    border: none;"
        "    height: 6px;"
        "    border-radius: 3px;"
        "}"
        "QSlider#seekSlider::add-page:horizontal {"
        "    background: rgba(7, 83, 119, 165);"
        "    border: none;"
        "    height: 6px;"
        "    border-radius: 3px;"
        "}"
        "QSlider#seekSlider::handle:horizontal {"
        "    background: transparent;"
        "    border: none;"
        "    width: 2px;"
        "    margin: -5px 0px;"
        "}"
        "QProgressBar#volumeMeter {"
        "    background: transparent;"
        "    border: none;"
        "    qproperty-barColor: #71E848;"
        "}"
        "QSlider:disabled::groove:horizontal, QSlider:disabled::add-page:horizontal {"
        "    background: #252d35;"
        "}"
        "QSlider:disabled::sub-page:horizontal {"
        "    background: #2d3b46;"
        "}"
        "QSlider:disabled::handle:horizontal {"
        "    background: #7e8994;"
        "    border-color: #7e8994;"
        "}"
    );
}

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

bool main_window::eventFilter(QObject *watched, QEvent *event)
{
    if (auto *button = qobject_cast<QAbstractButton *>(watched); button != nullptr)
    {
        if (event->type() == QEvent::Enter && !button->toolTip().isEmpty())
        {
            QToolTip::showText(button->mapToGlobal(QPoint(button->width() / 2, button->height())), button->toolTip(), button);
        }
        else if (event->type() == QEvent::Leave)
        {
            QToolTip::hideText();
        }
    }

    if (watched != title_drag_area_ && watched != title_bar_)
    {
        return QMainWindow::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseButtonDblClick)
    {
        auto *mouse_event = static_cast<QMouseEvent *>(event);
        if (mouse_event->button() == Qt::LeftButton)
        {
            toggle_window_maximized();
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonPress)
    {
        auto *mouse_event = static_cast<QMouseEvent *>(event);
        if (mouse_event->button() == Qt::LeftButton)
        {
            dragging_title_bar_ = true;
            drag_start_global_pos_ = mouse_event->globalPosition().toPoint();
            drag_start_window_pos_ = this->pos();
            drag_press_window_x_ = drag_start_global_pos_.x() - this->frameGeometry().x();
            if (!isMaximized() && windowHandle() != nullptr && windowHandle()->startSystemMove())
            {
                dragging_title_bar_ = false;
            }
            return true;
        }
    }

    if (event->type() == QEvent::MouseMove)
    {
        auto *mouse_event = static_cast<QMouseEvent *>(event);
        if (dragging_title_bar_ && (mouse_event->buttons() & Qt::LeftButton))
        {
            const QPoint global_pos = mouse_event->globalPosition().toPoint();
            if (isMaximized())
            {
                showNormal();
                update_title_maximize_button();
                drag_start_global_pos_ = global_pos;
                drag_start_window_pos_ = QPoint(global_pos.x() - drag_press_window_x_, global_pos.y() - (title_bar_->height() / 2));
                move(drag_start_window_pos_);
                if (windowHandle() != nullptr)
                {
                    windowHandle()->startSystemMove();
                    dragging_title_bar_ = false;
                }
                return true;
            }

            move(drag_start_window_pos_ + (global_pos - drag_start_global_pos_));
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonRelease)
    {
        dragging_title_bar_ = false;
        return true;
    }

    return QMainWindow::eventFilter(watched, event);
}

void main_window::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    update_media_title_text();
}

void main_window::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Open))
    {
        LOG_INFO("key open shortcut pressed");
        on_open_file();
    }
    else if (event->key() == Qt::Key_F11)
    {
        LOG_INFO("key fullscreen shortcut pressed");
        on_toggle_fullscreen();
    }
    else if (event->key() == Qt::Key_Left)
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

void main_window::toggle_window_maximized()
{
    if (isFullScreen())
    {
        return;
    }

    if (isMaximized())
    {
        showNormal();
    }
    else
    {
        showMaximized();
    }
    update_title_maximize_button();
}

void main_window::update_title_maximize_button()
{
    if (btn_title_maximize_ == nullptr)
    {
        return;
    }

    btn_title_maximize_->setText(isMaximized() ? "❐" : "□");
    btn_title_maximize_->setToolTip(isMaximized() ? "还原" : "最大化");
}

void main_window::set_media_title_text(const QString &text)
{
    media_title_full_text_ = text.isEmpty() ? QStringLiteral("视频播放器") : text;
    media_title_scroll_offset_ = 0;
    update_media_title_text();
}

void main_window::update_media_title_text()
{
    if (lbl_media_title_ == nullptr)
    {
        return;
    }

    QFontMetrics metrics(lbl_media_title_->font());
    const int available_width = qMax(1, lbl_media_title_->width() - 12);
    if (metrics.horizontalAdvance(media_title_full_text_) <= available_width)
    {
        if (title_scroll_timer_ != nullptr)
        {
            title_scroll_timer_->stop();
        }
        lbl_media_title_->setAlignment(Qt::AlignCenter);
        lbl_media_title_->setText(media_title_full_text_);
        return;
    }

    const QString scroll_text = media_title_full_text_ + QStringLiteral("      ");
    const int safe_length = qMax(1, scroll_text.size());
    media_title_scroll_offset_ %= safe_length;

    const QString rotated = scroll_text.mid(media_title_scroll_offset_) + scroll_text.left(media_title_scroll_offset_);
    QString visible_text;
    for (const QChar ch : rotated)
    {
        if (!visible_text.isEmpty() && metrics.horizontalAdvance(visible_text + ch) > available_width)
        {
            break;
        }
        visible_text.append(ch);
    }

    lbl_media_title_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    lbl_media_title_->setText(visible_text);
    if (title_scroll_timer_ != nullptr && !title_scroll_timer_->isActive())
    {
        title_scroll_timer_->start();
    }
}

void main_window::on_title_scroll_tick()
{
    ++media_title_scroll_offset_;
    update_media_title_text();
}

void main_window::update_volume_icon(int value)
{
    Q_UNUSED(value);
    if (lbl_vol_icon_low_ == nullptr)
    {
        return;
    }

    lbl_vol_icon_low_->setPixmap(QIcon(":/icons/volume-up.svg").pixmap(16, 16));
}

void main_window::on_open_file()
{
    LOG_INFO("on open file clicked");
    const QStringList filenames = QFileDialog::getOpenFileNames(
        this,
        "打开媒体文件",
        "",
        "Media Files (*.mp4 *.mkv *.avi *.mov *.flv *.webm *.mp3 *.flac *.wav *.aac *.ogg);;Video Files (*.mp4 *.mkv *.avi *.mov *.flv *.webm);;Audio Files (*.mp3 *.flac *.wav *.aac *.ogg);;All Files (*)");
    if (filenames.isEmpty())
    {
        LOG_INFO("open file cancelled");
        return;
    }

    if (playlist_view_->count() == 1 && playlist_view_->item(0)->data(Qt::UserRole).toString().isEmpty())
    {
        playlist_view_->clear();
    }

    int target_row = -1;
    for (const QString &filename : filenames)
    {
        LOG_INFO("open file selected {}", filename.toStdString());
        int row = -1;
        for (int i = 0; i < playlist_view_->count(); ++i)
        {
            if (playlist_view_->item(i)->data(Qt::UserRole).toString() == filename)
            {
                row = i;
                break;
            }
        }

        if (row < 0)
        {
            auto *item = new QListWidgetItem(QFileInfo(filename).fileName(), playlist_view_);
            item->setData(Qt::UserRole, filename);
            item->setToolTip(filename);
            row = playlist_view_->count() - 1;
        }

        if (target_row < 0)
        {
            target_row = row;
        }
    }

    lbl_playlist_count_->setText(QString("%1 个文件").arg(playlist_view_->count()));
    if (target_row >= 0)
    {
        play_playlist_row(target_row);
    }
}

void main_window::on_toggle_pause()
{
    if (!playing_)
    {
        play_playlist_row(playlist_view_->currentRow());
        return;
    }
    paused_ = !paused_;

    LOG_INFO("toggle pause state new state paused {}", paused_);

    btn_play_pause_->setIcon(QIcon(paused_ ? ":/icons/play.svg" : ":/icons/pause.svg"));
    btn_play_pause_->setToolTip(paused_ ? "播放" : "暂停");

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
    lbl_time_->setText("00:00:00 / 00:00:00");
    btn_play_pause_->setIcon(QIcon(":/icons/play.svg"));
    btn_play_pause_->setToolTip("播放/暂停");
}

void main_window::on_toggle_fullscreen()
{
    LOG_INFO("toggle fullscreen");
    if (isFullScreen())
    {
        showNormal();
        control_panel_->show();
        title_bar_->show();
        update_title_maximize_button();
    }
    else
    {
        showFullScreen();
        title_bar_->hide();
    }
}

void main_window::on_toggle_playlist()
{
    const bool visible = playlist_panel_->isVisible();
    playlist_panel_->setVisible(!visible);
    btn_playlist_->setChecked(!visible);
}

void main_window::on_play_previous()
{
    const int row = playlist_view_->currentRow();
    if (row > 0)
    {
        play_playlist_row(row - 1);
    }
}

void main_window::on_play_next()
{
    const int row = playlist_view_->currentRow();
    if (row >= 0 && row + 1 < playlist_view_->count())
    {
        play_playlist_row(row + 1);
    }
}

void main_window::on_playlist_item_activated(QListWidgetItem *item)
{
    if (item == nullptr)
    {
        return;
    }

    play_playlist_row(playlist_view_->row(item));
}

void main_window::on_volume_changed(int value)
{
    update_volume_icon(value);
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

    lbl_time_->setText(QString("%1 / %2").arg(format_time(current), format_time(duration_)));
}

void main_window::play_playlist_row(int row)
{
    if (row < 0 || row >= playlist_view_->count())
    {
        return;
    }

    QListWidgetItem *item = playlist_view_->item(row);
    if (item == nullptr)
    {
        return;
    }

    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty())
    {
        return;
    }

    stop_play();
    if (!start_play(path.toStdString()))
    {
        LOG_ERROR("failed to start play for {}", path.toStdString());
        QMessageBox::critical(this, "错误", "打开媒体文件失败");
        return;
    }

    current_media_path_ = path;
    playlist_view_->setCurrentRow(row);
    update_playlist_buttons();

    const QString display_name = QFileInfo(path).fileName();
    this->setWindowTitle(display_name + " - 视频播放器");
    set_media_title_text(display_name);
}

void main_window::update_playlist_buttons()
{
    btn_backward_->setEnabled(true);
    btn_forward_->setEnabled(true);
    btn_playlist_->setEnabled(true);
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
    if (video_decoder_ != nullptr)
    {
        video_decoder_->stop();
    }
    if (audio_decoder_ != nullptr)
    {
        audio_decoder_->stop();
    }

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
                                              lbl_time_->setText(QString("%1 / %2").arg(format_time(time), format_time(duration_)));
                                          }
                                      });
        });

    duration_ = demuxer_->duration();
    slider_seek_->setRange(0, static_cast<int>(duration_));
    slider_seek_->setEnabled(true);
    slider_seek_->setValue(0);
    lbl_time_->setText(QString("%1 / %2").arg(format_time(0.0), format_time(duration_)));

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
        audio_backend_->set_volume(volume_meter_ != nullptr ? volume_meter_->value() : 80);
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
    btn_play_pause_->setIcon(QIcon(":/icons/pause.svg"));
    btn_play_pause_->setToolTip("暂停");
    ui_timer_->start();
    this->setFocus();

    LOG_INFO("play started successfully");

    return true;
}
