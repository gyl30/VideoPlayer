#include <QDebug>
#include <QAbstractItemView>
#include <QAbstractButton>
#include <QFileInfo>
#include <QFontMetrics>
#include <QIcon>
#include <QKeySequence>
#include <QMouseEvent>
#include <QSettings>
#include <QTime>
#include <QToolTip>
#include <QUrl>
#include <QWindow>
#include "log.h"
#include "main_window.h"
#include "volumemeter.h"

namespace
{
constexpr const char *k_settings_org = "gyl30";
constexpr const char *k_settings_app = "VideoPlayer";
constexpr int k_resize_border_width = 8;

QString normalize_media_path(const QString &path)
{
    return QFileInfo(path).absoluteFilePath();
}

QString playback_position_key(const QString &path)
{
    return QStringLiteral("playback/positions/%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(normalize_media_path(path))));
}
}  // namespace

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

    title_drag_layout->addSpacing(18);

    auto *media_title_wrap = new QWidget(title_drag_area_);
    media_title_wrap->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *media_title_layout = new QHBoxLayout(media_title_wrap);
    media_title_layout->setContentsMargins(0, 0, 24, 0);
    media_title_layout->setSpacing(0);

    lbl_media_title_ = new QLabel("视频播放器", this);
    lbl_media_title_->setObjectName("mediaTitle");
    lbl_media_title_->setAlignment(Qt::AlignCenter);
    lbl_media_title_->setMinimumWidth(0);
    lbl_media_title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    lbl_media_title_->setAttribute(Qt::WA_TransparentForMouseEvents);
    media_title_layout->addWidget(lbl_media_title_, 1);

    title_drag_layout->addWidget(media_title_wrap, 1);
    title_layout->addWidget(title_drag_area_, 1);

    auto *title_button_box = new QWidget(title_bar_);
    title_button_box->setObjectName("titleButtonBox");
    title_button_box->setFixedWidth(142);

    auto *title_button_layout = new QHBoxLayout(title_button_box);
    title_button_layout->setContentsMargins(0, 0, 0, 0);
    title_button_layout->setSpacing(0);

    btn_title_minimize_ = new QPushButton(QIcon(":/icons/title-minimize.svg"), QString(), this);
    btn_title_minimize_->setObjectName("windowButton");
    btn_title_minimize_->setCursor(Qt::PointingHandCursor);
    btn_title_minimize_->setIconSize(QSize(14, 14));
    btn_title_minimize_->setToolTip("最小化");

    btn_title_maximize_ = new QPushButton(QIcon(":/icons/title-maximize.svg"), QString(), this);
    btn_title_maximize_->setObjectName("windowButton");
    btn_title_maximize_->setCursor(Qt::PointingHandCursor);
    btn_title_maximize_->setIconSize(QSize(14, 14));
    btn_title_maximize_->setToolTip("最大化");

    btn_title_close_ = new QPushButton(QIcon(":/icons/title-close.svg"), QString(), this);
    btn_title_close_->setObjectName("closeButton");
    btn_title_close_->setCursor(Qt::PointingHandCursor);
    btn_title_close_->setIconSize(QSize(14, 14));
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
    video_frame_layout_ = new QVBoxLayout(video_frame_);
    video_frame_layout_->setContentsMargins(0, 0, 0, 0);
    video_frame_layout_->setSpacing(0);

    video_widget_ = new video_widget(video_frame_);
    video_widget_->setObjectName("videoSurface");
    video_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    video_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
    video_frame_layout_->addWidget(video_widget_, 1);
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
    connect(playlist_view_, &QListWidget::currentRowChanged, this,
            [this](int)
            {
                update_playlist_buttons();
                save_playlist_state();
            });
    connect(video_widget_, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos)
            {
                QMenu menu(this);
                QAction *open_action = menu.addAction("打开");
                QAction *fullscreen_action = menu.addAction(is_video_fullscreen() ? "退出全屏" : "全屏");
                QAction *tile_action = menu.addAction("平铺播放");
                tile_action->setCheckable(true);
                tile_action->setChecked(video_widget_->current_display_mode() == video_widget::display_mode::fit);
                QAction *chosen = menu.exec(video_widget_->mapToGlobal(pos));
                if (chosen == open_action)
                {
                    on_open_file();
                }
                else if (chosen == fullscreen_action)
                {
                    on_toggle_fullscreen();
                }
                else if (chosen == tile_action)
                {
                    video_widget_->set_display_mode(tile_action->isChecked() ? video_widget::display_mode::fit : video_widget::display_mode::stretch);
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
    restore_persistent_state();
    update_volume_icon(volume_meter_ != nullptr ? volume_meter_->value() : 80);
    update_playlist_buttons();

    setMouseTracking(true);
    installEventFilter(this);
    for (auto *widget : findChildren<QWidget *>())
    {
        widget->setMouseTracking(true);
        widget->installEventFilter(this);
    }

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

Qt::Edges main_window::hit_test_resize_edges(const QPoint &global_pos) const
{
    if (isMaximized() || is_video_fullscreen())
    {
        return {};
    }

    const QRect frame = frameGeometry();
    Qt::Edges edges;

    if (global_pos.x() >= frame.left() && global_pos.x() <= frame.left() + k_resize_border_width)
    {
        edges |= Qt::LeftEdge;
    }
    else if (global_pos.x() <= frame.right() && global_pos.x() >= frame.right() - k_resize_border_width)
    {
        edges |= Qt::RightEdge;
    }

    if (global_pos.y() >= frame.top() && global_pos.y() <= frame.top() + k_resize_border_width)
    {
        edges |= Qt::TopEdge;
    }
    else if (global_pos.y() <= frame.bottom() && global_pos.y() >= frame.bottom() - k_resize_border_width)
    {
        edges |= Qt::BottomEdge;
    }

    return edges;
}

Qt::CursorShape main_window::cursor_shape_for_edges(Qt::Edges edges)
{
    if (edges == (Qt::TopEdge | Qt::LeftEdge) || edges == (Qt::BottomEdge | Qt::RightEdge))
    {
        return Qt::SizeFDiagCursor;
    }
    if (edges == (Qt::TopEdge | Qt::RightEdge) || edges == (Qt::BottomEdge | Qt::LeftEdge))
    {
        return Qt::SizeBDiagCursor;
    }
    if (edges == Qt::LeftEdge || edges == Qt::RightEdge)
    {
        return Qt::SizeHorCursor;
    }
    if (edges == Qt::TopEdge || edges == Qt::BottomEdge)
    {
        return Qt::SizeVerCursor;
    }
    return Qt::ArrowCursor;
}

bool main_window::handle_window_resize(QObject *watched, QEvent *event)
{
    auto *widget = qobject_cast<QWidget *>(watched);
    if (widget == nullptr || widget == video_fullscreen_window_)
    {
        return false;
    }

    if (isMaximized() || is_video_fullscreen())
    {
        widget->unsetCursor();
        return false;
    }

    if (event->type() == QEvent::MouseButtonPress)
    {
        auto *mouse_event = static_cast<QMouseEvent *>(event);
        if (mouse_event->button() == Qt::LeftButton)
        {
            const Qt::Edges edges = hit_test_resize_edges(mouse_event->globalPosition().toPoint());
            if (edges != Qt::Edges{} && windowHandle() != nullptr && windowHandle()->startSystemResize(edges))
            {
                return true;
            }
        }
        return false;
    }

    if (event->type() == QEvent::MouseMove)
    {
        auto *mouse_event = static_cast<QMouseEvent *>(event);
        if (mouse_event->buttons() == Qt::NoButton)
        {
            const Qt::Edges edges = hit_test_resize_edges(mouse_event->globalPosition().toPoint());
            if (edges == Qt::Edges{})
            {
                widget->unsetCursor();
            }
            else
            {
                widget->setCursor(cursor_shape_for_edges(edges));
            }
        }
        return false;
    }

    if (event->type() == QEvent::Leave)
    {
        widget->unsetCursor();
    }

    return false;
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
        "QPushButton#closeButton:hover {"
        "    background: rgba(255, 255, 255, 0.12);"
        "}"
        "QPushButton#closeButton:pressed {"
        "    background: rgba(0, 0, 0, 0.2);"
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
        "    background: transparent;"
        "    border: none;"
        "    color: #f5fbff;"
        "    border-radius: 2px;"
        "    min-width: 56px;"
        "    max-width: 56px;"
        "    min-height: 58px;"
        "    max-height: 58px;"
        "    font-size: 18px;"
        "}"
        "QPushButton#toolBlockButton:hover {"
        "    background: rgba(255, 255, 255, 0.12);"
        "    color: #ffffff;"
        "}"
        "QPushButton#toolBlockButton:checked {"
        "    background: rgba(36, 125, 189, 0.82);"
        "    color: #ffffff;"
        "}"
        "QPushButton#toolBlockButton:checked:hover {"
        "    background: rgba(51, 145, 213, 0.9);"
        "    color: #ffffff;"
        "}"
        "QPushButton#toolBlockButton:pressed {"
        "    background: rgba(8, 29, 49, 0.9);"
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
    save_persistent_state();
    stop_play();
}

void main_window::closeEvent(QCloseEvent *event)
{
    LOG_INFO("main window close event triggered");
    closing_ = true;
    if (video_fullscreen_window_ != nullptr)
    {
        LOG_INFO("closing video fullscreen window during main window shutdown");
        video_fullscreen_window_->close();
    }
    save_persistent_state();
    stop_play();
    QMainWindow::closeEvent(event);
}

bool main_window::eventFilter(QObject *watched, QEvent *event)
{
    if (handle_window_resize(watched, event))
    {
        return true;
    }

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

    if (watched == video_fullscreen_window_)
    {
        if (event->type() == QEvent::KeyPress)
        {
            auto *key_event = static_cast<QKeyEvent *>(event);
            if (key_event->matches(QKeySequence::Open))
            {
                LOG_INFO("key open shortcut pressed in video fullscreen");
                on_open_file();
                return true;
            }
            if (key_event->key() == Qt::Key_F11)
            {
                LOG_INFO("key fullscreen shortcut pressed in video fullscreen");
                on_toggle_fullscreen();
                return true;
            }
            if (key_event->key() == Qt::Key_Left)
            {
                LOG_INFO("key left pressed in video fullscreen");
                do_seek_relative(-15.0);
                return true;
            }
            if (key_event->key() == Qt::Key_Right)
            {
                LOG_INFO("key right pressed in video fullscreen");
                do_seek_relative(15.0);
                return true;
            }
            if (key_event->key() == Qt::Key_Space)
            {
                LOG_INFO("key space pressed in video fullscreen");
                on_toggle_pause();
                return true;
            }
            if (key_event->key() == Qt::Key_Escape)
            {
                LOG_INFO("key escape pressed in video fullscreen");
                on_toggle_fullscreen();
                return true;
            }
        }

        if (event->type() == QEvent::Close)
        {
            if (closing_)
            {
                LOG_INFO("video fullscreen close event allowed during shutdown");
                return false;
            }
            LOG_INFO("video fullscreen close event intercepted");
            exit_video_fullscreen();
            return true;
        }

        return QMainWindow::eventFilter(watched, event);
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
    else if (event->key() == Qt::Key_Escape && is_video_fullscreen())
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
    if (is_video_fullscreen())
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

    btn_title_maximize_->setIcon(QIcon(isMaximized() ? ":/icons/title-restore.svg" : ":/icons/title-maximize.svg"));
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

void main_window::restore_persistent_state()
{
    QSettings settings(k_settings_org, k_settings_app);

    if (volume_meter_ != nullptr)
    {
        const int saved_volume = qBound(0, settings.value("audio/volume", volume_meter_->value()).toInt(), 100);
        volume_meter_->setValue(saved_volume);
    }

    if (playlist_view_ == nullptr || lbl_playlist_count_ == nullptr)
    {
        return;
    }

    const QStringList saved_paths = settings.value("playlist/paths").toStringList();
    playlist_view_->clear();

    for (const QString &saved_path : saved_paths)
    {
        const QString normalized_path = normalize_media_path(saved_path);
        if (normalized_path.isEmpty())
        {
            continue;
        }

        auto *item = new QListWidgetItem(QFileInfo(normalized_path).fileName(), playlist_view_);
        item->setData(Qt::UserRole, normalized_path);
        item->setToolTip(normalized_path);
    }

    if (playlist_view_->count() == 0)
    {
        playlist_view_->addItem("打开文件后显示在这里");
        lbl_playlist_count_->setText("0 个文件");
        return;
    }

    lbl_playlist_count_->setText(QString("%1 个文件").arg(playlist_view_->count()));

    const int saved_row = settings.value("playlist/currentRow", 0).toInt();
    playlist_view_->setCurrentRow(qBound(0, saved_row, playlist_view_->count() - 1));
}

void main_window::save_persistent_state()
{
    save_current_playback_progress(true);
    save_playlist_state();
    if (volume_meter_ != nullptr)
    {
        save_volume_state(volume_meter_->value());
    }
}

void main_window::save_playlist_state()
{
    if (playlist_view_ == nullptr)
    {
        return;
    }

    QStringList playlist_paths;
    for (int i = 0; i < playlist_view_->count(); ++i)
    {
        QListWidgetItem *item = playlist_view_->item(i);
        if (item == nullptr)
        {
            continue;
        }

        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty())
        {
            playlist_paths.append(path);
        }
    }

    QSettings settings(k_settings_org, k_settings_app);
    settings.setValue("playlist/paths", playlist_paths);
    settings.setValue("playlist/currentRow", playlist_paths.isEmpty() ? -1 : playlist_view_->currentRow());
}

void main_window::save_volume_state(int value)
{
    QSettings settings(k_settings_org, k_settings_app);
    settings.setValue("audio/volume", qBound(0, value, 100));
}

void main_window::save_current_playback_progress(bool force)
{
    if (current_media_path_.isEmpty())
    {
        return;
    }

    double current = 0.0;
    if (clock_ != nullptr)
    {
        current = clock_->get();
    }
    else if (slider_seek_ != nullptr)
    {
        current = static_cast<double>(slider_seek_->value());
    }

    int current_second = qMax(0, static_cast<int>(current));
    if (duration_ > 1.0 && current >= duration_ - 2.0)
    {
        current_second = 0;
    }

    if (!force && current_second == last_saved_progress_second_)
    {
        return;
    }

    last_saved_progress_second_ = current_second;

    QSettings settings(k_settings_org, k_settings_app);
    settings.setValue(playback_position_key(current_media_path_), current_second);
}

void main_window::restore_playback_progress(const QString &path)
{
    if (demuxer_ == nullptr || slider_seek_ == nullptr || lbl_time_ == nullptr)
    {
        return;
    }

    QSettings settings(k_settings_org, k_settings_app);
    const int saved_second = settings.value(playback_position_key(path), 0).toInt();
    if (saved_second <= 0)
    {
        return;
    }

    if (duration_ > 1.0 && static_cast<double>(saved_second) >= duration_ - 2.0)
    {
        settings.setValue(playback_position_key(path), 0);
        return;
    }

    LOG_INFO("restoring playback progress path {} second {}", path.toStdString(), saved_second);
    last_saved_progress_second_ = saved_second;
    slider_seek_->setValue(saved_second);
    lbl_time_->setText(QString("%1 / %2").arg(format_time(static_cast<double>(saved_second)), format_time(duration_)));
    demuxer_->seek(static_cast<double>(saved_second));
}

bool main_window::is_video_fullscreen() const
{
    return video_fullscreen_window_ != nullptr && video_fullscreen_window_->isVisible();
}

void main_window::enter_video_fullscreen()
{
    if (video_widget_ == nullptr || video_frame_layout_ == nullptr || is_video_fullscreen())
    {
        return;
    }

    if (video_fullscreen_window_ == nullptr)
    {
        video_fullscreen_window_ = new QWidget(this, Qt::Window | Qt::FramelessWindowHint);
        video_fullscreen_window_->setObjectName("videoFullscreenWindow");
        video_fullscreen_window_->setFocusPolicy(Qt::StrongFocus);
        video_fullscreen_window_->setWindowTitle(this->windowTitle());
        video_fullscreen_window_->setWindowIcon(this->windowIcon());
        video_fullscreen_window_->setStyleSheet("background: #000000;");
        video_fullscreen_window_->installEventFilter(this);

        video_fullscreen_layout_ = new QVBoxLayout(video_fullscreen_window_);
        video_fullscreen_layout_->setContentsMargins(0, 0, 0, 0);
        video_fullscreen_layout_->setSpacing(0);
    }

    video_frame_layout_->removeWidget(video_widget_);
    video_widget_->setParent(video_fullscreen_window_);
    video_fullscreen_layout_->addWidget(video_widget_, 1);
    video_widget_->show();

    video_fullscreen_window_->showFullScreen();
    video_fullscreen_window_->activateWindow();
    video_fullscreen_window_->raise();
    video_fullscreen_window_->setFocus();
}

void main_window::exit_video_fullscreen()
{
    if (video_widget_ == nullptr || video_frame_layout_ == nullptr || video_fullscreen_window_ == nullptr ||
        video_fullscreen_layout_ == nullptr)
    {
        return;
    }

    video_fullscreen_layout_->removeWidget(video_widget_);
    video_widget_->setParent(video_frame_);
    video_frame_layout_->addWidget(video_widget_, 1);
    video_widget_->show();

    video_fullscreen_window_->hide();
    this->show();
    this->activateWindow();
    this->raise();
    this->setFocus();
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
        const QString normalized_path = normalize_media_path(filename);
        LOG_INFO("open file selected {}", normalized_path.toStdString());
        int row = -1;
        for (int i = 0; i < playlist_view_->count(); ++i)
        {
            if (playlist_view_->item(i)->data(Qt::UserRole).toString() == normalized_path)
            {
                row = i;
                break;
            }
        }

        if (row < 0)
        {
            auto *item = new QListWidgetItem(QFileInfo(normalized_path).fileName(), playlist_view_);
            item->setData(Qt::UserRole, normalized_path);
            item->setToolTip(normalized_path);
            row = playlist_view_->count() - 1;
        }

        if (target_row < 0)
        {
            target_row = row;
        }
    }

    lbl_playlist_count_->setText(QString("%1 个文件").arg(playlist_view_->count()));
    save_playlist_state();
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
    is_video_fullscreen() ? exit_video_fullscreen() : enter_video_fullscreen();
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
    save_volume_state(value);
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
    save_current_playback_progress();
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
    last_saved_progress_second_ = -1;
    playlist_view_->setCurrentRow(row);
    update_playlist_buttons();
    save_playlist_state();
    restore_playback_progress(path);

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

    save_current_playback_progress(true);

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
    current_media_path_.clear();
    last_saved_progress_second_ = -1;
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
    last_saved_progress_second_ = -1;
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
