#include <QDebug>
#include <QAbstractItemView>
#include <QAbstractButton>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QIcon>
#include <QCursor>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QSettings>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QStyle>
#include <QTime>
#include <QToolTip>
#include <QUrl>
#include <QWindow>
#include <cmath>
#include "log.h"
#include "main_window.h"
#include "playlist_name_dialog.h"
#include "playlist_management_dialog.h"
#include "volumemeter.h"

namespace
{
constexpr const char *k_settings_org = "gyl30";
constexpr const char *k_settings_app = "VideoPlayer";
constexpr int k_resize_border_width = 8;
constexpr int k_playlist_item_type_role = Qt::UserRole;
constexpr int k_playlist_id_role = Qt::UserRole + 1;
constexpr int k_playlist_row_role = Qt::UserRole + 2;
constexpr int k_playlist_type = 1;
constexpr int k_playlist_file_type = 2;

class seek_slider : public QSlider
{
   public:
    explicit seek_slider(QWidget *parent = nullptr) : QSlider(Qt::Horizontal, parent) {}

   protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
        {
            QSlider::mousePressEvent(event);
            return;
        }

        setFocus();
        setSliderDown(true);
        emit sliderPressed();
        update_slider_position(event);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!isSliderDown() || !(event->buttons() & Qt::LeftButton))
        {
            QSlider::mouseMoveEvent(event);
            return;
        }

        update_slider_position(event);
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || !isSliderDown())
        {
            QSlider::mouseReleaseEvent(event);
            return;
        }

        update_slider_position(event);
        setValue(sliderPosition());
        setSliderDown(false);
        emit sliderReleased();
        event->accept();
    }

   private:
    void update_slider_position(QMouseEvent *event)
    {
        const int x = std::clamp(static_cast<int>(event->position().x()), 0, std::max(width(), 0));
        const int value = QStyle::sliderValueFromPosition(minimum(), maximum(), x, std::max(width(), 1));
        setSliderPosition(value);
        emit sliderMoved(value);
    }
};

QString normalize_media_path(const QString &path)
{
    return QFileInfo(path).absoluteFilePath();
}

QString playback_position_key(const QString &path)
{
    return QStringLiteral("playback/positions/%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(normalize_media_path(path))));
}

QString format_playback_rate_text(double rate)
{
    QString text = QString::number(rate, 'f', 2);
    while (text.contains('.') && text.endsWith('0'))
    {
        text.chop(1);
    }
    if (text.endsWith('.'))
    {
        text.chop(1);
    }
    return text + "x";
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
    video_widget_->setContextMenuPolicy(Qt::NoContextMenu);
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
    playlist_header_layout->setSpacing(0);

    btn_playlist_create_ = new QPushButton(QIcon(":/icons/playlist-add.svg"), QString(), this);
    btn_playlist_create_->setObjectName("playlistHeaderButton");
    btn_playlist_create_->setCursor(Qt::PointingHandCursor);
    btn_playlist_create_->setIconSize(QSize(13, 13));
    btn_playlist_create_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btn_playlist_create_->setToolTip("新建播放列表");
    btn_playlist_manage_ = new QPushButton(QIcon(":/icons/playlist-manage.svg"), QString(), this);
    btn_playlist_manage_->setObjectName("playlistHeaderButton");
    btn_playlist_manage_->setCursor(Qt::PointingHandCursor);
    btn_playlist_manage_->setIconSize(QSize(13, 13));
    btn_playlist_manage_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    btn_playlist_manage_->setToolTip("管理播放列表");

    auto *playlist_header_group = new QWidget(this);
    playlist_header_group->setObjectName("playlistHeaderGroup");
    auto *playlist_header_group_layout = new QHBoxLayout(playlist_header_group);
    playlist_header_group_layout->setContentsMargins(4, 4, 4, 4);
    playlist_header_group_layout->setSpacing(2);
    playlist_header_group_layout->addWidget(btn_playlist_create_, 1);
    playlist_header_group_layout->addWidget(btn_playlist_manage_, 1);

    playlist_header_layout->addWidget(playlist_header_group, 1);
    playlist_layout->addLayout(playlist_header_layout);

    playlist_view_ = new QTreeWidget(this);
    playlist_view_->setObjectName("playlistView");
    playlist_view_->setHeaderHidden(true);
    playlist_view_->setIndentation(0);
    playlist_view_->setExpandsOnDoubleClick(false);
    playlist_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    playlist_view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    playlist_view_->setContextMenuPolicy(Qt::NoContextMenu);
    playlist_layout->addWidget(playlist_view_, 1);
    playlist_panel_->hide();
    content_layout->addWidget(playlist_panel_);

    main_layout->addWidget(content_widget, 1);

    control_panel_ = new QWidget(this);
    control_panel_->setObjectName("controlPanel");
    control_panel_->setFixedHeight(104);

    auto *control_layout = new QVBoxLayout(control_panel_);
    control_layout->setContentsMargins(0, 0, 0, 0);
    control_layout->setSpacing(0);

    auto *seek_row = new QWidget(this);
    seek_row->setObjectName("seekRow");
    seek_row->setFixedHeight(24);

    auto *seek_layout = new QHBoxLayout(seek_row);
    seek_layout->setContentsMargins(10, 0, 10, 0);
    seek_layout->setSpacing(0);

    auto *btn_seek_back = new QPushButton(QIcon(":/icons/skip-backward-fill.svg"), QString(), this);
    btn_seek_back->setObjectName("seekEdgeButton");
    btn_seek_back->setCursor(Qt::PointingHandCursor);
    btn_seek_back->setIconSize(QSize(14, 14));
    btn_seek_back->setToolTip("快退 15 秒");

    slider_seek_ = new seek_slider(this);
    slider_seek_->setObjectName("seekSlider");
    slider_seek_->setRange(0, 0);
    slider_seek_->setEnabled(false);
    slider_seek_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    slider_seek_->setFixedHeight(16);

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
    control_row->setContentsMargins(20, 5, 20, 6);
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
    btn_open_media_ = new QPushButton(QIcon(":/icons/open-media.svg"), QString(), this);
    btn_open_media_->setObjectName("toolBlockButton");
    btn_open_media_->setCursor(Qt::PointingHandCursor);
    btn_open_media_->setIconSize(QSize(18, 18));
    btn_open_media_->setToolTip("打开媒体文件");
    btn_screenshot_ = new QPushButton(QIcon(":/icons/camera-fill.svg"), QString(), this);
    btn_screenshot_->setObjectName("toolBlockButton");
    btn_screenshot_->setCursor(Qt::PointingHandCursor);
    btn_screenshot_->setIconSize(QSize(18, 18));
    btn_screenshot_->setToolTip("保存当前画面");
    btn_video_fullscreen_ = new QPushButton(QIcon(":/icons/fullscreen-enter.svg"), QString(), this);
    btn_video_fullscreen_->setObjectName("toolBlockButton");
    btn_video_fullscreen_->setCursor(Qt::PointingHandCursor);
    btn_video_fullscreen_->setIconSize(QSize(18, 18));
    btn_video_fullscreen_->setToolTip("全屏");

    btn_sequential_playback_ = new QPushButton("顺播", this);
    btn_sequential_playback_->setObjectName("controlButtonWide");
    btn_sequential_playback_->setCursor(Qt::PointingHandCursor);
    btn_sequential_playback_->setCheckable(true);
    btn_sequential_playback_->setChecked(false);
    btn_sequential_playback_->setToolTip("播放结束后自动播放下一项");

    btn_audio_only_ = new QPushButton("仅音频", this);
    btn_audio_only_->setObjectName("controlButtonWide");
    btn_audio_only_->setCursor(Qt::PointingHandCursor);
    btn_audio_only_->setCheckable(true);
    btn_audio_only_->setChecked(false);
    btn_audio_only_->setToolTip("隐藏视频画面，仅播放音频");

    btn_playback_rate_ = new QPushButton(this);
    btn_playback_rate_->setObjectName("controlButtonWide");
    btn_playback_rate_->setCursor(Qt::PointingHandCursor);
    btn_playback_rate_->setToolTip("播放速度");
    playback_rate_menu_ = new QMenu(this);
    playback_rate_menu_->setStyleSheet(
        "QMenu {"
        "    background: #0b1929;"
        "    color: #d8e7f6;"
        "    border: 1px solid #1e7dbd;"
        "    padding: 6px;"
        "}"
        "QMenu::item {"
        "    padding: 7px 16px;"
        "    margin: 2px 4px;"
        "    border-radius: 4px;"
        "}"
        "QMenu::item:selected, QMenu::item:checked {"
        "    background: #174a68;"
        "    color: #ffffff;"
        "}"
        "QMenu::indicator {"
        "    width: 0px;"
        "    height: 0px;"
        "}"
    );
    for (double rate : {0.5, 0.75, 1.0, 1.25, 1.5, 2.0})
    {
        QAction *rate_action = playback_rate_menu_->addAction(format_playback_rate_text(rate));
        rate_action->setCheckable(true);
        rate_action->setData(rate);
        connect(rate_action,
                &QAction::triggered,
                this,
                [this, rate]()
                {
                    set_playback_rate(rate);
                });
    }
    update_playback_rate_button();

    control_row->addWidget(lbl_time_);
    control_row->addStretch(1);
    control_row->addWidget(btn_stop_);
    control_row->addWidget(btn_backward_);
    control_row->addWidget(btn_play_pause_);
    control_row->addWidget(btn_forward_);
    control_row->addWidget(btn_sequential_playback_);
    control_row->addWidget(btn_audio_only_);
    control_row->addStretch(1);
    control_row->addWidget(btn_playback_rate_);
    control_row->addWidget(lbl_vol_icon_low_);
    control_row->addWidget(volume_meter_);
    control_row->addSpacing(12);
    control_row->addWidget(btn_open_media_);
    control_row->addWidget(btn_screenshot_);
    control_row->addWidget(btn_video_fullscreen_);
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
    connect(slider_seek_,
            &QSlider::sliderMoved,
            this,
            [this](int value)
            {
                lbl_time_->setText(QString("%1 / %2").arg(format_time(static_cast<double>(value)), format_time(duration_)));
            });
    connect(btn_open_media_, &QPushButton::clicked, this, &main_window::on_open_file);
    connect(btn_screenshot_, &QPushButton::clicked, this, &main_window::on_save_screenshot);
    connect(btn_video_fullscreen_, &QPushButton::clicked, this, &main_window::on_toggle_fullscreen);
    connect(btn_playlist_, &QPushButton::clicked, this, &main_window::on_toggle_playlist);
    connect(btn_audio_only_, &QPushButton::toggled, this, &main_window::on_audio_only_toggled);
    connect(btn_playback_rate_, &QPushButton::clicked, this, &main_window::show_playback_rate_menu);
    connect(btn_playlist_create_, &QPushButton::clicked, this, &main_window::on_create_playlist);
    connect(btn_playlist_manage_, &QPushButton::clicked, this, [this]() { open_playlist_management_dialog(); });
    connect(playlist_view_, &QTreeWidget::itemDoubleClicked, this, &main_window::on_playlist_item_activated);

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
    update_fullscreen_button();
    update_screenshot_button();
    update_playlist_buttons();
    update_playlist_header_buttons();

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

    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseMove && event->type() != QEvent::Leave)
    {
        return false;
    }

    if (isMaximized() || is_video_fullscreen())
    {
        if (widget->testAttribute(Qt::WA_SetCursor))
        {
            widget->unsetCursor();
        }
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
                if (widget->testAttribute(Qt::WA_SetCursor))
                {
                    widget->unsetCursor();
                }
            }
            else
            {
                const Qt::CursorShape shape = cursor_shape_for_edges(edges);
                if (widget->cursor().shape() != shape)
                {
                    widget->setCursor(shape);
                }
            }
        }
        return false;
    }

    if (event->type() == QEvent::Leave)
    {
        if (widget->testAttribute(Qt::WA_SetCursor))
        {
            widget->unsetCursor();
        }
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
        "QWidget#playlistHeaderGroup {"
        "    background: transparent;"
        "}"
        "QPushButton#playlistHeaderButton {"
        "    background: transparent;"
        "    border: none;"
        "    min-height: 32px;"
        "    border-radius: 4px;"
        "}"
        "QPushButton#playlistHeaderButton:hover {"
        "    background: rgba(255, 255, 255, 0.1);"
        "}"
        "QPushButton#playlistHeaderButton:disabled {"
        "    color: rgba(216, 224, 234, 0.35);"
        "}"
        "QPushButton#playlistHeaderButton:pressed {"
        "    background: rgba(0, 0, 0, 0.18);"
        "}"
        "QTreeWidget#playlistView {"
        "    background: transparent;"
        "    border: none;"
        "    color: #b8c4cf;"
        "    outline: none;"
        "}"
        "QTreeWidget#playlistView::item {"
        "    height: 34px;"
        "    padding-left: 6px;"
        "    padding-right: 10px;"
        "    border-radius: 4px;"
        "}"
        "QTreeWidget#playlistView::item:hover {"
        "    background: #1b2631;"
        "    color: #f3f8fc;"
        "}"
        "QTreeWidget#playlistView::item:selected {"
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
        "    min-height: 46px;"
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
        "    min-height: 48px;"
        "    max-height: 48px;"
        "    font-size: 18px;"
        "}"
        "QPushButton#toolBlockButton:hover {"
        "    background: rgba(255, 255, 255, 0.12);"
        "    color: #ffffff;"
        "}"
        "QPushButton#toolBlockButton:disabled {"
        "    color: rgba(245, 251, 255, 0.32);"
        "    background: transparent;"
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
        "    background: #f4fff0;"
        "    border: 2px solid #b9ff95;"
        "    width: 8px;"
        "    height: 8px;"
        "    margin: -3px 0;"
        "    border-radius: 6px;"
        "}"
        "QSlider#seekSlider::handle:horizontal:hover {"
        "    background: #ffffff;"
        "    border-color: #d2ffb9;"
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
    if (event->isAccepted())
    {
        LOG_INFO("main window close accepted, quitting application");
        QCoreApplication::quit();
    }
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

void main_window::update_fullscreen_button()
{
    if (btn_video_fullscreen_ == nullptr)
    {
        return;
    }

    const bool fullscreen = is_video_fullscreen();
    const bool has_video = demuxer_ != nullptr && demuxer_->video_index() >= 0;
    btn_video_fullscreen_->setIcon(QIcon(fullscreen ? ":/icons/fullscreen-exit.svg" : ":/icons/fullscreen-enter.svg"));
    if (fullscreen)
    {
        btn_video_fullscreen_->setEnabled(true);
        btn_video_fullscreen_->setToolTip("退出全屏");
    }
    else if (audio_only_mode_)
    {
        btn_video_fullscreen_->setEnabled(false);
        btn_video_fullscreen_->setToolTip("仅音频模式下不可全屏");
    }
    else if (!has_video)
    {
        btn_video_fullscreen_->setEnabled(false);
        btn_video_fullscreen_->setToolTip("当前媒体没有视频画面");
    }
    else
    {
        btn_video_fullscreen_->setEnabled(true);
        btn_video_fullscreen_->setToolTip("全屏");
    }
}

void main_window::update_screenshot_button()
{
    if (btn_screenshot_ == nullptr)
    {
        return;
    }

    const bool has_video = demuxer_ != nullptr && demuxer_->video_index() >= 0;
    const bool has_frame = video_widget_ != nullptr && video_widget_->has_frame();
    if (audio_only_mode_)
    {
        btn_screenshot_->setEnabled(false);
        btn_screenshot_->setToolTip("仅音频模式下不可截图");
    }
    else if (!has_video)
    {
        btn_screenshot_->setEnabled(false);
        btn_screenshot_->setToolTip("当前媒体没有视频画面");
    }
    else if (!has_frame)
    {
        btn_screenshot_->setEnabled(false);
        btn_screenshot_->setToolTip("等待视频画面后可截图");
    }
    else
    {
        btn_screenshot_->setEnabled(true);
        btn_screenshot_->setToolTip("保存当前画面");
    }
}

void main_window::update_playback_rate_button()
{
    if (btn_playback_rate_ != nullptr)
    {
        btn_playback_rate_->setText(format_playback_rate_text(playback_rate_));
        btn_playback_rate_->setToolTip(QString("播放速度：%1").arg(format_playback_rate_text(playback_rate_)));
    }

    if (playback_rate_menu_ == nullptr)
    {
        return;
    }

    for (QAction *action : playback_rate_menu_->actions())
    {
        const double item_rate = action->data().toDouble();
        action->setChecked(std::abs(item_rate - playback_rate_) < 0.0001);
    }
}

void main_window::show_playback_rate_menu()
{
    if (btn_playback_rate_ == nullptr || playback_rate_menu_ == nullptr)
    {
        return;
    }

    update_playback_rate_button();
    playback_rate_menu_->popup(btn_playback_rate_->mapToGlobal(QPoint(0, btn_playback_rate_->height())));
}

void main_window::set_playback_rate(double rate)
{
    const double normalized_rate = std::clamp(rate, 0.5, 2.0);
    if (std::abs(playback_rate_ - normalized_rate) < 0.0001)
    {
        return;
    }

    LOG_INFO("setting playback rate {} -> {}", playback_rate_, normalized_rate);
    playback_rate_ = normalized_rate;

    update_playback_rate_button();

    if (audio_backend_ != nullptr)
    {
        audio_backend_->set_playback_rate(playback_rate_);
    }
    else if (clock_ != nullptr)
    {
        clock_->set_rate(playback_rate_);
    }
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

void main_window::refresh_playlist_view()
{
    if (playlist_view_ == nullptr)
    {
        return;
    }

    QSignalBlocker blocker(playlist_view_);
    playlist_view_->clear();

    const QString active_id = active_playlist_id();
    const bool has_playing_item = playing_ && !current_playback_playlist_id_.isEmpty() && current_playback_row_ >= 0;
    const QIcon playing_icon(":/icons/play.svg");
    const QBrush playing_brush(QColor("#83d7ff"));
    QTreeWidgetItem *current_item = nullptr;
    for (const playlist_entry &entry : playlist_store_.playlists())
    {
        auto *playlist_item = new QTreeWidgetItem(playlist_view_);
        playlist_item->setText(0, entry.name);
        playlist_item->setData(0, k_playlist_item_type_role, k_playlist_type);
        playlist_item->setData(0, k_playlist_id_role, entry.id);
        playlist_item->setExpanded(true);

        for (int row = 0; row < entry.paths.size(); ++row)
        {
            const QString &path = entry.paths[row];
            auto *file_item = new QTreeWidgetItem(playlist_item);
            file_item->setText(0, QFileInfo(path).fileName());
            file_item->setData(0, k_playlist_item_type_role, k_playlist_file_type);
            file_item->setData(0, k_playlist_id_role, entry.id);
            file_item->setData(0, k_playlist_row_role, row);
            file_item->setToolTip(0, path);

            if (entry.id == current_playback_playlist_id_ && row == current_playback_row_)
            {
                QFont font = file_item->font(0);
                font.setBold(true);
                file_item->setFont(0, font);
                file_item->setForeground(0, playing_brush);
                file_item->setIcon(0, playing_icon);
            }

            if (!has_playing_item && entry.id == active_id && row == entry.current_row)
            {
                current_item = file_item;
            }
        }

        if (!has_playing_item && entry.id == active_id && current_item == nullptr)
        {
            current_item = playlist_item;
        }
    }

    if (current_item != nullptr)
    {
        playlist_view_->setCurrentItem(current_item);
    }

    update_playlist_header_buttons();
}

void main_window::set_active_playlist(const QString &playlist_id)
{
    if (playlist_id.isEmpty() || !playlist_store_.set_active_playlist(playlist_id))
    {
        return;
    }

    refresh_playlist_view();
    update_playlist_buttons();
    save_playlist_state();
}

QString main_window::active_playlist_id() const { return playlist_store_.active_playlist_id(); }

void main_window::open_playlist_management_dialog()
{
    playlist_management_dialog dialog(playlist_store_, this);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    apply_playlist_management_changes(dialog.result_store());
}

void main_window::apply_playlist_management_changes(const playlist_store &store)
{
    const QString old_active_playlist_id = active_playlist_id();
    playlist_store_ = store;

    if (playing_ && !current_media_path_.isEmpty())
    {
        QString matched_playlist_id;
        int matched_row = -1;

        if (!current_playback_playlist_id_.isEmpty())
        {
            matched_row = playlist_store_.index_of_path(current_playback_playlist_id_, current_media_path_);
            if (matched_row >= 0)
            {
                matched_playlist_id = current_playback_playlist_id_;
            }
        }

        if (matched_playlist_id.isEmpty())
        {
            for (const playlist_entry &entry : playlist_store_.playlists())
            {
                const int row = playlist_store_.index_of_path(entry.id, current_media_path_);
                if (row >= 0)
                {
                    matched_playlist_id = entry.id;
                    matched_row = row;
                    break;
                }
            }
        }

        current_playback_playlist_id_ = matched_playlist_id;
        current_playback_row_ = matched_row;
        if (!matched_playlist_id.isEmpty())
        {
            playlist_store_.set_active_playlist(matched_playlist_id);
            playlist_store_.set_current_row(matched_playlist_id, matched_row);
        }
        else if (playlist_store_.playlist_by_id(old_active_playlist_id) != nullptr)
        {
            playlist_store_.set_active_playlist(old_active_playlist_id);
        }
    }
    else if (playlist_store_.playlist_by_id(old_active_playlist_id) != nullptr)
    {
        playlist_store_.set_active_playlist(old_active_playlist_id);
    }

    refresh_playlist_view();
    update_playlist_buttons();
    save_playlist_state();
}

bool main_window::is_playlist_item(const QTreeWidgetItem *item) const
{
    return item != nullptr && item->data(0, k_playlist_item_type_role).toInt() == k_playlist_type;
}

bool main_window::is_playlist_file_item(const QTreeWidgetItem *item) const
{
    return item != nullptr && item->data(0, k_playlist_item_type_role).toInt() == k_playlist_file_type;
}

QString main_window::playlist_id_for_item(const QTreeWidgetItem *item) const
{
    if (item == nullptr)
    {
        return {};
    }
    return item->data(0, k_playlist_id_role).toString();
}

int main_window::playlist_row_for_item(const QTreeWidgetItem *item) const
{
    if (!is_playlist_file_item(item))
    {
        return -1;
    }
    return item->data(0, k_playlist_row_role).toInt();
}

QString main_window::playback_playlist_id() const
{
    return current_playback_playlist_id_;
}

int main_window::playback_playlist_row() const { return current_playback_row_; }

void main_window::restore_persistent_state()
{
    QSettings settings(k_settings_org, k_settings_app);

    const QRect saved_geometry = settings.value("window/geometry").toRect();
    const bool saved_maximized = settings.value("window/maximized", false).toBool();
    if (saved_geometry.isValid())
    {
        setGeometry(saved_geometry);
    }
    if (saved_maximized)
    {
        showMaximized();
    }
    update_title_maximize_button();

    if (volume_meter_ != nullptr)
    {
        const int saved_volume = qBound(0, settings.value("audio/volume", volume_meter_->value()).toInt(), 100);
        volume_meter_->setValue(saved_volume);
    }

    playlist_store_.load(settings);
    refresh_playlist_view();
}

void main_window::save_persistent_state()
{
    QSettings settings(k_settings_org, k_settings_app);
    if (is_video_fullscreen() && fullscreen_restore_geometry_.isValid())
    {
        settings.setValue("window/geometry", fullscreen_restore_geometry_);
        settings.setValue("window/maximized", fullscreen_restore_maximized_);
    }
    else
    {
        const QRect window_geometry = isMaximized() ? normalGeometry() : geometry();
        if (window_geometry.isValid())
        {
            settings.setValue("window/geometry", window_geometry);
        }
        settings.setValue("window/maximized", isMaximized());
    }

    save_current_playback_progress(true);
    save_playlist_state();
    if (volume_meter_ != nullptr)
    {
        save_volume_state(volume_meter_->value());
    }
}

void main_window::save_playlist_state()
{
    QSettings settings(k_settings_org, k_settings_app);
    playlist_store_.save(settings);
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
        video_fullscreen_window_ = new QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint);
        video_fullscreen_window_->setObjectName("videoFullscreenWindow");
        video_fullscreen_window_->setFocusPolicy(Qt::StrongFocus);
        video_fullscreen_window_->setStyleSheet("background: #000000;");
        video_fullscreen_window_->installEventFilter(this);

        video_fullscreen_layout_ = new QVBoxLayout(video_fullscreen_window_);
        video_fullscreen_layout_->setContentsMargins(0, 0, 0, 0);
        video_fullscreen_layout_->setSpacing(0);
    }

    fullscreen_restore_maximized_ = isMaximized();
    if (fullscreen_restore_maximized_)
    {
        fullscreen_restore_geometry_ = normalGeometry();
    }
    else
    {
        fullscreen_restore_geometry_ = geometry();
    }

    video_fullscreen_window_->setWindowTitle(this->windowTitle());
    video_fullscreen_window_->setWindowIcon(this->windowIcon());
    video_frame_layout_->removeWidget(video_widget_);
    video_widget_->setParent(video_fullscreen_window_);
    video_fullscreen_layout_->addWidget(video_widget_, 1);
    video_widget_->show();

    video_fullscreen_window_->showFullScreen();
    this->hide();
    video_fullscreen_window_->activateWindow();
    video_fullscreen_window_->raise();
    video_fullscreen_window_->setFocus();
    update_fullscreen_button();
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
    if (fullscreen_restore_maximized_)
    {
        showMaximized();
    }
    else
    {
        if (fullscreen_restore_geometry_.isValid())
        {
            setGeometry(fullscreen_restore_geometry_);
        }
        showNormal();
    }
    update_title_maximize_button();
    update_fullscreen_button();
    this->activateWindow();
    this->raise();
    this->setFocus();
}

void main_window::on_open_file()
{
    open_files_into_playlist(active_playlist_id());
}

void main_window::open_files_into_playlist(const QString &playlist_id)
{
    LOG_INFO("on open file clicked");
    QWidget *dialog_parent = this;
    if (is_video_fullscreen() && video_fullscreen_window_ != nullptr)
    {
        dialog_parent = video_fullscreen_window_;
    }

    const QStringList filenames = QFileDialog::getOpenFileNames(
        dialog_parent,
        "打开媒体文件",
        "",
        "Media Files (*.mp4 *.mkv *.avi *.mov *.flv *.webm *.mp3 *.flac *.wav *.aac *.ogg);;Video Files (*.mp4 *.mkv *.avi *.mov *.flv *.webm);;Audio Files (*.mp3 *.flac *.wav *.aac *.ogg);;All Files (*)");
    if (filenames.isEmpty())
    {
        LOG_INFO("open file cancelled");
        return;
    }

    const QString target_playlist_id = playlist_id.isEmpty() ? active_playlist_id() : playlist_id;
    int target_row = -1;
    for (const QString &filename : filenames)
    {
        const QString normalized_path = normalize_media_path(filename);
        LOG_INFO("open file selected {}", normalized_path.toStdString());
        int row = playlist_store_.index_of_path(target_playlist_id, normalized_path);
        if (row < 0 && playlist_store_.add_path(target_playlist_id, normalized_path))
        {
            const playlist_entry *entry = playlist_store_.playlist_by_id(target_playlist_id);
            if (entry != nullptr)
            {
                row = static_cast<int>(entry->paths.size() - 1);
            }
        }

        if (target_row < 0)
        {
            target_row = row;
        }
    }

    refresh_playlist_view();
    save_playlist_state();
    if (target_row >= 0)
    {
        play_playlist_item(target_playlist_id, target_row);
        if (is_video_fullscreen() && video_fullscreen_window_ != nullptr)
        {
            video_fullscreen_window_->activateWindow();
            video_fullscreen_window_->raise();
            video_fullscreen_window_->setFocus();
        }
    }
}

void main_window::on_create_playlist()
{
    bool accepted = false;
    const QString name = playlist_name_dialog::get_text(this, "新建播放列表", "播放列表名称", "创建", "", &accepted);
    if (!accepted)
    {
        return;
    }

    const QString playlist_id = playlist_store_.create_playlist(name);
    if (playlist_id.isEmpty())
    {
        return;
    }

    set_active_playlist(playlist_id);
}

void main_window::on_toggle_pause()
{
    if (!playing_)
    {
        play_playlist_row(playlist_store_.current_row(active_playlist_id()));
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

void main_window::update_playlist_header_buttons()
{
    if (btn_playlist_create_ != nullptr)
    {
        btn_playlist_create_->setEnabled(true);
        btn_playlist_create_->setToolTip("新建播放列表");
    }
    if (btn_playlist_manage_ != nullptr)
    {
        btn_playlist_manage_->setEnabled(playlist_store_.playlist_count() > 0);
        btn_playlist_manage_->setToolTip("管理播放列表");
    }
}

void main_window::on_toggle_playlist()
{
    const bool visible = playlist_panel_->isVisible();
    playlist_panel_->setVisible(!visible);
    update_playlist_buttons();
}

void main_window::on_save_screenshot()
{
    if (video_widget_ == nullptr)
    {
        return;
    }

    QWidget *dialog_parent = this;
    if (is_video_fullscreen() && video_fullscreen_window_ != nullptr)
    {
        dialog_parent = video_fullscreen_window_;
    }

    QString pictures_dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pictures_dir.isEmpty() && !current_media_path_.isEmpty())
    {
        pictures_dir = QFileInfo(current_media_path_).absolutePath();
    }
    if (pictures_dir.isEmpty())
    {
        pictures_dir = QDir::currentPath();
    }

    QString base_name = current_media_path_.isEmpty() ? QStringLiteral("frame") : QFileInfo(current_media_path_).completeBaseName();
    if (base_name.isEmpty())
    {
        base_name = QStringLiteral("frame");
    }

    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString default_path = QDir(pictures_dir).filePath(QString("%1_%2.png").arg(base_name, timestamp));
    QString output_path = QFileDialog::getSaveFileName(dialog_parent, "保存截图", default_path, "PNG 图片 (*.png)");
    if (output_path.isEmpty())
    {
        return;
    }
    if (!output_path.endsWith(".png", Qt::CaseInsensitive))
    {
        output_path += ".png";
    }

    if (!video_widget_->save_current_frame(output_path))
    {
        QMessageBox::critical(dialog_parent, "错误", "保存截图失败");
        return;
    }

    QToolTip::showText(QCursor::pos(), QString("截图已保存到\n%1").arg(QDir::toNativeSeparators(output_path)), dialog_parent);
}

void main_window::on_play_previous()
{
    const QString playlist_id = playing_ ? playback_playlist_id() : active_playlist_id();
    const int row = playing_ ? playback_playlist_row() : playlist_store_.current_row(playlist_id);
    if (row > 0)
    {
        play_playlist_item(playlist_id, row - 1);
    }
}

void main_window::on_play_next()
{
    const QString playlist_id = playing_ ? playback_playlist_id() : active_playlist_id();
    const playlist_entry *entry = playlist_store_.playlist_by_id(playlist_id);
    const int row = playing_ ? playback_playlist_row() : playlist_store_.current_row(playlist_id);
    if (entry != nullptr && row >= 0 && row + 1 < entry->paths.size())
    {
        play_playlist_item(playlist_id, row + 1);
    }
}

void main_window::on_playlist_item_activated(QTreeWidgetItem *item, int)
{
    if (is_playlist_file_item(item))
    {
        play_playlist_item(playlist_id_for_item(item), playlist_row_for_item(item));
    }
    else if (is_playlist_item(item))
    {
        item->setExpanded(!item->isExpanded());
    }
}

void main_window::on_audio_only_toggled(bool checked)
{
    audio_only_mode_ = checked;
    if (audio_only_mode_ && video_widget_ != nullptr)
    {
        video_widget_->clear();
    }
    update_fullscreen_button();
    update_screenshot_button();
}

void main_window::on_video_frame_ready(std::shared_ptr<media_frame> frame)
{
    if (!playing_ || audio_only_mode_ || video_widget_ == nullptr)
    {
        return;
    }

    video_widget_->on_frame_ready(std::move(frame));
    update_screenshot_button();
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

    const double raw_current = clock_->get();
    const double current = duration_ > 0.0 ? std::clamp(raw_current, 0.0, duration_) : raw_current;

    if (demuxer_ != nullptr && demuxer_->eof_reached() && duration_ > 0.0 && current >= duration_ - 0.1)
    {
        finish_playback();
        return;
    }

    if (!slider_seek_->isSliderDown())
    {
        slider_seek_->setValue(static_cast<int>(current));
    }

    lbl_time_->setText(QString("%1 / %2").arg(format_time(current), format_time(duration_)));
    save_current_playback_progress();
}

void main_window::play_playlist_item(const QString &playlist_id, int row)
{
    const playlist_entry *entry = playlist_store_.playlist_by_id(playlist_id);
    if (entry == nullptr || row < 0 || row >= entry->paths.size())
    {
        return;
    }

    const QString path = entry->paths[row];
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
    current_playback_playlist_id_ = playlist_id;
    current_playback_row_ = row;
    playlist_store_.set_active_playlist(playlist_id);
    playlist_store_.set_current_row(playlist_id, row);
    refresh_playlist_view();
    update_playlist_buttons();
    save_playlist_state();
    restore_playback_progress(path);

    const QString display_name = QFileInfo(path).fileName();
    this->setWindowTitle(display_name + " - 视频播放器");
    if (video_fullscreen_window_ != nullptr)
    {
        video_fullscreen_window_->setWindowTitle(this->windowTitle());
        video_fullscreen_window_->setWindowIcon(this->windowIcon());
    }
    set_media_title_text(display_name);
}

void main_window::play_playlist_row(int row) { play_playlist_item(active_playlist_id(), row); }

void main_window::update_playlist_buttons()
{
    const playlist_entry *entry = playing_ ? playlist_store_.playlist_by_id(playback_playlist_id()) : playlist_store_.active_playlist();
    const int file_count = entry == nullptr ? 0 : static_cast<int>(entry->paths.size());
    const int row = playing_ ? playback_playlist_row() : playlist_store_.current_row(active_playlist_id());
    const bool has_current = row >= 0 && row < file_count;

    btn_backward_->setEnabled(has_current && row > 0);
    btn_forward_->setEnabled(has_current && row + 1 < file_count);
    if (btn_playlist_ != nullptr)
    {
        const bool visible = playlist_panel_ != nullptr && playlist_panel_->isVisible();
        btn_playlist_->setEnabled(true);
        btn_playlist_->setChecked(visible);
        btn_playlist_->setToolTip(visible ? "隐藏播放列表" : "显示播放列表");
    }
}

void main_window::finish_playback()
{
    const QString playlist_id = playback_playlist_id();
    const playlist_entry *entry = playlist_store_.playlist_by_id(playlist_id);
    const int current_row = playback_playlist_row();
    if (btn_sequential_playback_ != nullptr && btn_sequential_playback_->isChecked() && entry != nullptr)
    {
        const int next_row = current_row + 1;
        if (next_row >= 0 && next_row < entry->paths.size())
        {
            if (!entry->paths[next_row].isEmpty())
            {
                LOG_INFO("playback reached end continuing to next row {}", next_row);
                play_playlist_item(playlist_id, next_row);
                return;
            }
        }
    }

    LOG_INFO("playback reached end resetting ui");
    stop_play();

    const int end_value = qMax(0, static_cast<int>(duration_));
    slider_seek_->setValue(end_value);
    slider_seek_->setEnabled(false);
    lbl_time_->setText(QString("%1 / %2").arg(format_time(duration_), format_time(duration_)));
    btn_play_pause_->setIcon(QIcon(":/icons/play.svg"));
    btn_play_pause_->setToolTip("播放");
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
        disconnect(sync_thread_.get(), nullptr, this, nullptr);
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
        QCoreApplication::removePostedEvents(video_widget_, QEvent::MetaCall);
        video_widget_->clear();
    }
    current_media_path_.clear();
    current_playback_playlist_id_.clear();
    current_playback_row_ = -1;
    last_saved_progress_second_ = -1;
    refresh_playlist_view();
    update_playlist_buttons();
    update_fullscreen_button();
    update_screenshot_button();
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
    clock_->set_rate(playback_rate_);

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
        audio_backend_->set_playback_rate(playback_rate_);
        audio_backend_->set_volume(volume_meter_ != nullptr ? volume_meter_->value() : 80);
    }

    if (demuxer_->video_index() >= 0)
    {
        sync_thread_ = std::make_unique<video_sync_thread>(
            video_frame_queue_.get(), video_pkt_queue_.get(), demuxer_->time_base(demuxer_->video_index()), clock_.get());

        connect(sync_thread_.get(), &video_sync_thread::frame_ready, this, &main_window::on_video_frame_ready);

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
    update_fullscreen_button();
    update_screenshot_button();

    LOG_INFO("play started successfully");

    return true;
}
