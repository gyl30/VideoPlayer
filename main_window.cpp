#include <QDebug>
#include <QAbstractItemView>
#include <QAbstractButton>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDir>
#include <QDirIterator>
#include <QDialog>
#include <QDropEvent>
#include <QFileInfo>
#include <QFile>
#include <QFontMetrics>
#include <QHeaderView>
#include <QIcon>
#include <QCursor>
#include <QKeySequence>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QSettings>
#include <QShortcut>
#include <QSet>
#include <QTableWidget>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QStyle>
#include <QTime>
#include <QToolTip>
#include <QUrl>
#include <QWindow>
#include <algorithm>
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
constexpr int k_compact_control_bar_width = 1240;
constexpr int k_narrow_control_bar_width = 900;
constexpr int k_playback_history_limit = 100;
constexpr int k_resume_prompt_minimum_second = 10;
constexpr int k_resume_prompt_near_end_margin_second = 30;
constexpr int k_recent_history_menu_limit = 20;
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

QString playback_history_entry_group_key(const QString &path)
{
    return QStringLiteral("history/items/%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(normalize_media_path(path))));
}

void touch_playback_history_order(QSettings &settings, const QString &path)
{
    const QString normalized_path = normalize_media_path(path);
    QStringList order = settings.value("history/order").toStringList();
    order.removeAll(normalized_path);
    order.prepend(normalized_path);

    while (order.size() > k_playback_history_limit)
    {
        const QString removed_path = order.takeLast();
        settings.remove(playback_history_entry_group_key(removed_path));
    }

    settings.setValue("history/order", order);
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

QString format_frame_rate_text(AVRational frame_rate)
{
    if (frame_rate.num <= 0 || frame_rate.den <= 0)
    {
        return {};
    }

    QString text = QString::number(av_q2d(frame_rate), 'f', 2);
    while (text.contains('.') && text.endsWith('0'))
    {
        text.chop(1);
    }
    if (text.endsWith('.'))
    {
        text.chop(1);
    }
    return text;
}

int codec_parameters_channels(const AVCodecParameters *codec_par)
{
    if (codec_par == nullptr)
    {
        return 0;
    }

#if LIBAVCODEC_VERSION_MAJOR >= 59
    return codec_par->ch_layout.nb_channels;
#else
    return codec_par->channels;
#endif
}

QString media_file_dialog_filter()
{
    return QStringLiteral(
        "Media Files (*.mp4 *.mkv *.avi *.mov *.flv *.webm *.mp3 *.flac *.wav *.aac *.ogg *.m4a *.opus);;"
        "Video Files (*.mp4 *.mkv *.avi *.mov *.flv *.webm);;"
        "Audio Files (*.mp3 *.flac *.wav *.aac *.ogg *.m4a *.opus);;"
        "All Files (*)");
}

const QSet<QString> &supported_media_extensions()
{
    static const QSet<QString> extensions = {
        "mp4", "mkv", "avi", "mov", "flv", "webm", "mp3", "flac", "wav", "aac", "ogg", "m4a", "opus"};
    return extensions;
}

bool is_supported_media_file(const QFileInfo &file_info)
{
    return file_info.isFile() && supported_media_extensions().contains(file_info.suffix().toLower());
}

QStringList collect_media_files_from_directory(const QString &directory_path)
{
    QStringList files;
    QDirIterator iterator(directory_path, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QString next_path = iterator.next();
        const QFileInfo file_info(next_path);
        if (!is_supported_media_file(file_info))
        {
            continue;
        }

        files.append(normalize_media_path(file_info.absoluteFilePath()));
    }

    std::sort(files.begin(), files.end(), [](const QString &lhs, const QString &rhs)
    {
        const int name_compare = QString::localeAwareCompare(QFileInfo(lhs).fileName(), QFileInfo(rhs).fileName());
        if (name_compare != 0)
        {
            return name_compare < 0;
        }

        return QString::localeAwareCompare(lhs, rhs) < 0;
    });
    return files;
}

QStringList load_playlist_file_paths(const QString &playlist_path)
{
    QFile file(playlist_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return {};
    }

    const QString content = QString::fromUtf8(file.readAll());
    const QStringList lines = content.split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::SkipEmptyParts);
    const QDir base_dir = QFileInfo(playlist_path).dir();

    QStringList files;
    QSet<QString> seen_paths;
    for (const QString &line : lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#'))
        {
            continue;
        }

        const QString candidate_path = QDir::isRelativePath(trimmed) ? base_dir.absoluteFilePath(trimmed) : trimmed;
        const QString normalized_path = normalize_media_path(candidate_path);
        const QFileInfo file_info(normalized_path);
        if (!file_info.exists() || !is_supported_media_file(file_info) || seen_paths.contains(normalized_path))
        {
            continue;
        }

        seen_paths.insert(normalized_path);
        files.append(normalized_path);
    }

    return files;
}

QStringList local_media_files_from_urls(const QList<QUrl> &urls)
{
    QStringList files;
    QSet<QString> seen_paths;
    for (const QUrl &url : urls)
    {
        if (!url.isLocalFile())
        {
            continue;
        }

        const QString normalized_path = normalize_media_path(url.toLocalFile());
        QFileInfo file_info(normalized_path);
        if (!file_info.exists())
        {
            continue;
        }

        if (file_info.isDir())
        {
            for (const QString &media_path : collect_media_files_from_directory(normalized_path))
            {
                if (seen_paths.contains(media_path))
                {
                    continue;
                }

                seen_paths.insert(media_path);
                files.append(media_path);
            }
            continue;
        }

        if (!is_supported_media_file(file_info) || seen_paths.contains(normalized_path))
        {
            continue;
        }

        seen_paths.insert(normalized_path);
        files.append(normalized_path);
    }
    return files;
}

QString popup_menu_stylesheet()
{
    return QStringLiteral(
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
        "}");
}

struct playback_history_item
{
    QString path;
    QString title;
    int position = 0;
    int duration = 0;
};

QList<playback_history_item> load_playback_history(QSettings &settings, int limit)
{
    QList<playback_history_item> items;
    const QStringList order = settings.value("history/order").toStringList();
    for (const QString &path : order)
    {
        if (path.isEmpty())
        {
            continue;
        }

        const QString entry_group = playback_history_entry_group_key(path);
        playback_history_item item;
        item.path = settings.value(entry_group + "/path", path).toString();
        item.title = settings.value(entry_group + "/title").toString();
        item.position = settings.value(entry_group + "/position", 0).toInt();
        item.duration = settings.value(entry_group + "/duration", 0).toInt();
        if (item.path.isEmpty() || !QFileInfo::exists(item.path))
        {
            continue;
        }
        if (item.title.isEmpty())
        {
            item.title = QFileInfo(item.path).fileName();
        }

        items.append(std::move(item));
        if (items.size() >= limit)
        {
            break;
        }
    }
    return items;
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
    title_bar_->setAcceptDrops(true);

    auto *title_layout = new QHBoxLayout(title_bar_);
    title_layout->setContentsMargins(0, 0, 0, 0);
    title_layout->setSpacing(0);

    title_drag_area_ = new QWidget(title_bar_);
    title_drag_area_->setObjectName("titleDragArea");
    title_drag_area_->installEventFilter(this);
    title_drag_area_->setCursor(Qt::ArrowCursor);
    title_drag_area_->setAcceptDrops(true);

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

    lbl_media_title_ = new QLabel(QString(), this);
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
    content_widget->setAcceptDrops(true);
    auto *content_layout = new QHBoxLayout(content_widget);
    content_layout->setContentsMargins(0, 0, 0, 0);
    content_layout->setSpacing(0);

    video_frame_ = new QFrame(this);
    video_frame_->setObjectName("videoFrame");
    video_frame_->setAcceptDrops(true);
    video_frame_layout_ = new QVBoxLayout(video_frame_);
    video_frame_layout_->setContentsMargins(0, 0, 0, 0);
    video_frame_layout_->setSpacing(0);

    video_widget_ = new video_widget(video_frame_);
    video_widget_->setObjectName("videoSurface");
    video_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    video_widget_->setContextMenuPolicy(Qt::NoContextMenu);
    video_widget_->setAcceptDrops(true);
    video_frame_layout_->addWidget(video_widget_, 1);

    content_layout->addWidget(video_frame_, 1);

    playlist_panel_ = new QFrame(this);
    playlist_panel_->setObjectName("playlistPanel");
    playlist_panel_->setFixedWidth(238);
    playlist_panel_->setAcceptDrops(true);

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
    playlist_view_->setContextMenuPolicy(Qt::CustomContextMenu);
    playlist_view_->setAcceptDrops(true);
    playlist_layout->addWidget(playlist_view_, 1);
    playlist_panel_->hide();
    content_layout->addWidget(playlist_panel_);

    main_layout->addWidget(content_widget, 1);

    control_panel_ = new QWidget(this);
    control_panel_->setObjectName("controlPanel");
    control_panel_->setFixedHeight(104);
    control_panel_->setAcceptDrops(true);

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

    auto *control_bar_layout = new QVBoxLayout(control_bar);
    control_bar_layout->setContentsMargins(20, 5, 20, 6);
    control_bar_layout->setSpacing(6);

    primary_control_row_widget_ = new QWidget(control_bar);
    primary_control_row_layout_ = new QHBoxLayout(primary_control_row_widget_);
    primary_control_row_layout_->setContentsMargins(0, 0, 0, 0);
    primary_control_row_layout_->setSpacing(10);

    secondary_control_row_widget_ = new QWidget(control_bar);
    secondary_control_row_layout_ = new QHBoxLayout(secondary_control_row_widget_);
    secondary_control_row_layout_->setContentsMargins(0, 0, 0, 0);
    secondary_control_row_layout_->setSpacing(8);

    tertiary_control_row_widget_ = new QWidget(control_bar);
    tertiary_control_row_layout_ = new QHBoxLayout(tertiary_control_row_widget_);
    tertiary_control_row_layout_->setContentsMargins(0, 0, 0, 0);
    tertiary_control_row_layout_->setSpacing(8);

    control_bar_layout->addWidget(primary_control_row_widget_);
    control_bar_layout->addWidget(secondary_control_row_widget_);
    control_bar_layout->addWidget(tertiary_control_row_widget_);

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
    btn_open_media_->setToolTip("打开媒体");
    btn_screenshot_ = new QPushButton(QIcon(":/icons/camera-fill.svg"), QString(), this);
    btn_screenshot_->setObjectName("toolBlockButton");
    btn_screenshot_->setCursor(Qt::PointingHandCursor);
    btn_screenshot_->setIconSize(QSize(18, 18));
    btn_screenshot_->setToolTip("保存当前画面");
    btn_screenshot_->hide();
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
    btn_sequential_playback_->hide();

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
    open_media_menu_ = new QMenu(this);
    open_media_menu_->setStyleSheet(popup_menu_stylesheet());
    QAction *open_file_action = open_media_menu_->addAction("打开文件");
    QAction *open_folder_action = open_media_menu_->addAction("打开文件夹");
    QAction *import_playlist_action = open_media_menu_->addAction("导入播放列表");
    playlist_manage_menu_ = new QMenu(this);
    playlist_manage_menu_->setStyleSheet(popup_menu_stylesheet());
    QAction *manage_playlist_action = playlist_manage_menu_->addAction("管理播放列表");
    QAction *export_playlist_action = playlist_manage_menu_->addAction("导出当前播放列表");
    playback_rate_menu_ = new QMenu(this);
    playback_rate_menu_->setStyleSheet(popup_menu_stylesheet());
    recent_history_menu_ = new QMenu(this);
    recent_history_menu_->setStyleSheet(popup_menu_stylesheet());
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
    connect(open_file_action, &QAction::triggered, this, &main_window::on_open_file);
    connect(open_folder_action, &QAction::triggered, this, &main_window::on_open_folder);
    connect(import_playlist_action, &QAction::triggered, this, &main_window::on_import_playlist);
    connect(manage_playlist_action, &QAction::triggered, this, [this]() { open_playlist_management_dialog(); });
    connect(export_playlist_action, &QAction::triggered, this, &main_window::on_export_playlist);
    connect(btn_open_media_, &QPushButton::clicked, this, &main_window::show_open_media_menu);
    connect(btn_screenshot_, &QPushButton::clicked, this, &main_window::on_save_screenshot);
    connect(btn_video_fullscreen_, &QPushButton::clicked, this, &main_window::on_toggle_fullscreen);
    connect(btn_playlist_, &QPushButton::clicked, this, &main_window::on_toggle_playlist);
    connect(btn_audio_only_, &QPushButton::toggled, this, &main_window::on_audio_only_toggled);
    connect(btn_playback_rate_, &QPushButton::clicked, this, &main_window::show_playback_rate_menu);
    connect(btn_playlist_create_, &QPushButton::clicked, this, &main_window::on_create_playlist);
    connect(btn_playlist_manage_, &QPushButton::clicked, this, &main_window::show_playlist_manage_menu);
    connect(playlist_view_, &QTreeWidget::customContextMenuRequested, this, &main_window::show_playlist_context_menu);
    connect(playlist_view_, &QTreeWidget::itemDoubleClicked, this, &main_window::on_playlist_item_activated);
    connect(playlist_view_,
            &QTreeWidget::itemExpanded,
            this,
            [this](QTreeWidgetItem *item)
            {
                update_playlist_item_icon(item);
            });
    connect(playlist_view_,
            &QTreeWidget::itemCollapsed,
            this,
            [this](QTreeWidgetItem *item)
            {
                update_playlist_item_icon(item);
            });

    connect(volume_meter_, &volume_meter::value_changed, this, &main_window::on_volume_changed);
    connect(btn_title_minimize_, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(btn_title_maximize_, &QPushButton::clicked, this, &main_window::toggle_window_maximized);
    connect(btn_title_close_, &QPushButton::clicked, this, &QWidget::close);

    ui_timer_ = new QTimer(this);
    ui_timer_->setInterval(200);
    connect(ui_timer_, &QTimer::timeout, this, &main_window::on_update_ui);

    title_scroll_timer_ = new QTimer(this);
    title_scroll_timer_->setInterval(320);
    connect(title_scroll_timer_, &QTimer::timeout, this, &main_window::on_title_scroll_tick);

    set_media_title_text(QString());
    install_playback_shortcuts(this);
    restore_persistent_state();
    update_volume_icon(volume_meter_ != nullptr ? volume_meter_->value() : 80);
    update_fullscreen_button();
    update_screenshot_button();
    update_playlist_buttons();
    update_playlist_header_buttons();
    update_control_layout_mode();

    playlist_scrollbar_hide_timer_ = new QTimer(this);
    playlist_scrollbar_hide_timer_->setSingleShot(true);
    playlist_scrollbar_hide_timer_->setInterval(1200);
    connect(playlist_scrollbar_hide_timer_,
            &QTimer::timeout,
            this,
            [this]()
            {
                if (!is_cursor_in_playlist_panel())
                {
                    set_playlist_scrollbar_visible(false);
                }
            });
    set_playlist_scrollbar_visible(false);

    setMouseTracking(true);
    setAcceptDrops(true);
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
        "QTreeWidget#playlistView QScrollBar:vertical {"
        "    background: transparent;"
        "    width: 10px;"
        "    margin: 4px 2px 4px 0;"
        "}"
        "QTreeWidget#playlistView QScrollBar::handle:vertical {"
        "    background: rgba(131, 215, 255, 0.38);"
        "    border-radius: 4px;"
        "    min-height: 28px;"
        "}"
        "QTreeWidget#playlistView QScrollBar::handle:vertical:hover {"
        "    background: rgba(131, 215, 255, 0.56);"
        "}"
        "QTreeWidget#playlistView QScrollBar::add-line:vertical, QTreeWidget#playlistView QScrollBar::sub-line:vertical {"
        "    height: 0;"
        "}"
        "QTreeWidget#playlistView QScrollBar::add-page:vertical, QTreeWidget#playlistView QScrollBar::sub-page:vertical {"
        "    background: transparent;"
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
    if (watched == video_widget_ && event != nullptr && event->type() == QEvent::Resize)
    {
        update_media_info_overlay_geometry();
    }

    if (watched == video_fullscreen_window_ && event != nullptr && event->type() == QEvent::Resize)
    {
        update_media_info_overlay_geometry();
    }

    if (handle_file_drop(watched, event))
    {
        return true;
    }

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

    if (is_playlist_region_object(watched))
    {
        if (event->type() == QEvent::Enter || event->type() == QEvent::MouseMove)
        {
            if (playlist_scrollbar_hide_timer_ != nullptr)
            {
                playlist_scrollbar_hide_timer_->stop();
            }
            set_playlist_scrollbar_visible(true);
        }
        else if (event->type() == QEvent::Leave)
        {
            schedule_hide_playlist_scrollbar();
        }
    }

    if (watched == video_fullscreen_window_)
    {
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
    update_control_layout_mode();
    update_media_info_overlay_geometry();
}

void main_window::keyPressEvent(QKeyEvent *event)
{
    QMainWindow::keyPressEvent(event);
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

void main_window::install_playback_shortcuts(QWidget *target)
{
    if (target == nullptr || target->property("playbackShortcutsInstalled").toBool())
    {
        return;
    }

    auto install_shortcut =
        [this, target](const QKeySequence &sequence, const std::function<void()> &handler)
        {
            auto *shortcut = new QShortcut(sequence, target);
            shortcut->setContext(Qt::WidgetWithChildrenShortcut);
            connect(shortcut,
                    &QShortcut::activated,
                    this,
                    [handler]()
                    {
                        handler();
                    });
        };

    install_shortcut(QKeySequence::Open,
                     [this]()
                     {
                         LOG_INFO("key open shortcut pressed");
                         on_open_file();
                     });
    install_shortcut(QKeySequence(Qt::Key_F),
                     [this]()
                     {
                         LOG_INFO("key fullscreen shortcut pressed");
                         on_toggle_fullscreen();
                     });
    install_shortcut(QKeySequence(Qt::Key_F11),
                     [this]()
                     {
                         LOG_INFO("key fullscreen shortcut pressed");
                         on_toggle_fullscreen();
                     });
    install_shortcut(QKeySequence(Qt::Key_Left),
                     [this]()
                     {
                         LOG_INFO("key left pressed");
                         do_seek_relative(-5.0);
                     });
    install_shortcut(QKeySequence(Qt::Key_Right),
                     [this]()
                     {
                         LOG_INFO("key right pressed");
                         do_seek_relative(5.0);
                     });
    install_shortcut(QKeySequence(Qt::Key_Space),
                     [this]()
                     {
                         LOG_INFO("key space pressed");
                         on_toggle_pause();
                     });
    install_shortcut(QKeySequence(Qt::Key_Up),
                     [this]()
                     {
                         if (volume_meter_ == nullptr)
                         {
                             return;
                         }
                         LOG_INFO("key up pressed");
                         volume_meter_->setValue(qBound(0, volume_meter_->value() + 5, 100));
                     });
    install_shortcut(QKeySequence(Qt::Key_Down),
                     [this]()
                     {
                         if (volume_meter_ == nullptr)
                         {
                             return;
                         }
                         LOG_INFO("key down pressed");
                         volume_meter_->setValue(qBound(0, volume_meter_->value() - 5, 100));
                     });
    install_shortcut(QKeySequence(Qt::Key_Escape),
                     [this]()
                     {
                         if (!is_video_fullscreen())
                         {
                             return;
                         }
                         LOG_INFO("key escape pressed in fullscreen");
                         on_toggle_fullscreen();
                     });
    install_shortcut(QKeySequence(Qt::CTRL | Qt::Key_H),
                     [this]()
                     {
                         LOG_INFO("key recent history shortcut pressed");
                         show_recent_history_menu();
                     });
    install_shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S),
                     [this]()
                     {
                         LOG_INFO("key screenshot shortcut pressed");
                         if (audio_only_mode_ || video_widget_ == nullptr || !video_widget_->has_frame())
                         {
                             QWidget *tooltip_parent = is_video_fullscreen() && video_fullscreen_window_ != nullptr ? video_fullscreen_window_ : this;
                             QToolTip::showText(QCursor::pos(), "当前没有可截图的视频画面", tooltip_parent);
                             return;
                         }

                         on_save_screenshot();
                     });
    install_shortcut(QKeySequence(Qt::CTRL | Qt::Key_L),
                     [this]()
                     {
                         if (btn_sequential_playback_ == nullptr)
                         {
                             return;
                         }

                         btn_sequential_playback_->setChecked(!btn_sequential_playback_->isChecked());
                         QWidget *tooltip_parent = is_video_fullscreen() && video_fullscreen_window_ != nullptr ? video_fullscreen_window_ : this;
                         QToolTip::showText(QCursor::pos(),
                                            btn_sequential_playback_->isChecked() ? "已开启顺播" : "已关闭顺播",
                                            tooltip_parent);
                     });
    install_shortcut(QKeySequence(Qt::CTRL | Qt::Key_I),
                     [this]()
                     {
                         LOG_INFO("key media info shortcut pressed");
                         toggle_media_info_overlay();
                     });
    install_shortcut(QKeySequence(QStringLiteral("?")),
                     [this]()
                     {
                         LOG_INFO("key shortcuts help pressed");
                         show_shortcuts_help();
                     });

    target->setProperty("playbackShortcutsInstalled", true);
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

void main_window::set_playlist_scrollbar_visible(bool visible)
{
    playlist_scrollbar_visible_ = visible;
    if (playlist_view_ == nullptr || playlist_panel_ == nullptr)
    {
        return;
    }

    QScrollBar *scrollbar = playlist_view_->verticalScrollBar();
    if (scrollbar == nullptr)
    {
        return;
    }

    const bool should_show = visible && playlist_panel_->isVisible() && scrollbar->maximum() > 0;
    scrollbar->setVisible(should_show);
}

void main_window::schedule_hide_playlist_scrollbar()
{
    if (playlist_scrollbar_hide_timer_ == nullptr || !playlist_panel_->isVisible())
    {
        return;
    }

    if (is_cursor_in_playlist_panel())
    {
        return;
    }

    playlist_scrollbar_hide_timer_->start();
}

bool main_window::is_cursor_in_playlist_panel() const
{
    if (playlist_panel_ == nullptr || !playlist_panel_->isVisible())
    {
        return false;
    }

    const QPoint local_pos = playlist_panel_->mapFromGlobal(QCursor::pos());
    return playlist_panel_->rect().contains(local_pos);
}

bool main_window::is_playlist_region_object(const QObject *watched) const
{
    if (playlist_panel_ == nullptr || watched == nullptr)
    {
        return false;
    }

    if (watched == playlist_panel_)
    {
        return true;
    }

    const auto *widget = qobject_cast<const QWidget *>(watched);
    return widget != nullptr && playlist_panel_->isAncestorOf(const_cast<QWidget *>(widget));
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

void main_window::show_recent_history_menu()
{
    if (recent_history_menu_ == nullptr)
    {
        return;
    }

    QSettings settings(k_settings_org, k_settings_app);
    const QList<playback_history_item> items = load_playback_history(settings, k_recent_history_menu_limit);

    recent_history_menu_->clear();

    if (items.isEmpty())
    {
        QAction *empty_action = recent_history_menu_->addAction("暂无播放历史");
        empty_action->setEnabled(false);
    }
    else
    {
        for (const playback_history_item &item : items)
        {
            QString action_text = item.title;
            if (item.position > 0)
            {
                action_text += QString("  ·  %1").arg(format_time(static_cast<double>(item.position)));
            }

            QAction *action = recent_history_menu_->addAction(action_text);
            action->setToolTip(QDir::toNativeSeparators(item.path));
            connect(action, &QAction::triggered, this, [this, path = item.path]()
            {
                open_files_into_playlist(active_playlist_id(), QStringList{path});
            });
        }

        recent_history_menu_->addSeparator();
        QAction *clear_action = recent_history_menu_->addAction("清空播放历史");
        connect(clear_action, &QAction::triggered, this, &main_window::clear_recent_history);
    }

    QWidget *menu_parent = is_video_fullscreen() && video_fullscreen_window_ != nullptr ? video_fullscreen_window_ : this;
    const QSize menu_size = recent_history_menu_->sizeHint();
    const int x = std::max(0, (menu_parent->width() - menu_size.width()) / 2);
    const int y = title_bar_ != nullptr ? title_bar_->height() + 8 : 8;
    recent_history_menu_->popup(menu_parent->mapToGlobal(QPoint(x, y)));
}

void main_window::clear_recent_history()
{
    QSettings settings(k_settings_org, k_settings_app);
    const QStringList order = settings.value("history/order").toStringList();
    for (const QString &path : order)
    {
        settings.remove(playback_history_entry_group_key(path));
        settings.remove(playback_position_key(path));
    }
    settings.remove("history/order");
}

void main_window::toggle_media_info_overlay()
{
    media_info_overlay_enabled_ = !media_info_overlay_enabled_;
    update_media_info_overlay();
}

void main_window::update_media_info_overlay()
{
    if (video_widget_ == nullptr)
    {
        return;
    }

    const bool has_video = demuxer_ != nullptr && demuxer_->video_index() >= 0;
    const bool should_show = media_info_overlay_enabled_ && playing_ && has_video && !audio_only_mode_;
    if (!should_show)
    {
        video_widget_->set_media_info_overlay_visible(false);
        return;
    }

    QStringList lines;

    if (!current_media_path_.isEmpty())
    {
        lines.append(QString("<span style=\"color:#8ecfff;\">文件</span> %1").arg(QFileInfo(current_media_path_).fileName().toHtmlEscaped()));
    }

    if (demuxer_ != nullptr)
    {
        QString container_text = demuxer_->format_name();
        if (!container_text.isEmpty())
        {
            container_text = container_text.toUpper();
        }

        QString duration_text;
        if (duration_ > 0.0)
        {
            duration_text = format_time(duration_);
        }

        QStringList media_parts;
        if (!container_text.isEmpty())
        {
            media_parts.append(container_text);
        }
        if (!duration_text.isEmpty())
        {
            media_parts.append(duration_text);
        }
        if (!media_parts.isEmpty())
        {
            lines.append(QString("<span style=\"color:#8ecfff;\">媒体</span> %1").arg(media_parts.join(" · ").toHtmlEscaped()));
        }

        const int video_index = demuxer_->video_index();
        AVCodecParameters *video_par = demuxer_->codec_par(video_index);
        if (video_par != nullptr)
        {
            QStringList video_parts;
            video_parts.append(QString::fromUtf8(avcodec_get_name(video_par->codec_id)).toUpper());
            if (video_par->width > 0 && video_par->height > 0)
            {
                video_parts.append(QString("%1x%2").arg(video_par->width).arg(video_par->height));
            }

            const QString fps_text = format_frame_rate_text(demuxer_->frame_rate(video_index));
            if (!fps_text.isEmpty())
            {
                video_parts.append(fps_text + " fps");
            }

            if (video_decoder_ != nullptr)
            {
                video_parts.append(video_decoder_->using_hardware_decode() ? "硬解" : "软解");
            }

            lines.append(QString("<span style=\"color:#8ecfff;\">视频</span> %1").arg(video_parts.join(" · ").toHtmlEscaped()));
        }

        const int audio_index = demuxer_->audio_index();
        AVCodecParameters *audio_par = demuxer_->codec_par(audio_index);
        if (audio_par != nullptr)
        {
            QStringList audio_parts;
            audio_parts.append(QString::fromUtf8(avcodec_get_name(audio_par->codec_id)).toUpper());
            if (audio_par->sample_rate > 0)
            {
                audio_parts.append(QString("%1 Hz").arg(audio_par->sample_rate));
            }
            const int channels = codec_parameters_channels(audio_par);
            if (channels > 0)
            {
                audio_parts.append(QString("%1 声道").arg(channels));
            }

            lines.append(QString("<span style=\"color:#8ecfff;\">音频</span> %1").arg(audio_parts.join(" · ").toHtmlEscaped()));
        }
    }

    lines.append(QString("<span style=\"color:#8ecfff;\">状态</span> %1").arg(format_playback_rate_text(playback_rate_).toHtmlEscaped()));

    video_widget_->set_media_info_overlay_html(lines.join("<br>"));
    video_widget_->set_media_info_overlay_visible(true);
}

void main_window::update_media_info_overlay_geometry()
{
    if (video_widget_ == nullptr)
    {
        return;
    }

    video_widget_->update();
}

void main_window::show_shortcuts_help()
{
    QWidget *dialog_parent = is_video_fullscreen() && video_fullscreen_window_ != nullptr ? video_fullscreen_window_ : this;
    QDialog dialog(dialog_parent);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setModal(true);
    dialog.setWindowTitle("快捷键说明");
    dialog.resize(520, 420);
    dialog.setObjectName("shortcutsHelpDialog");
    dialog.setStyleSheet(
        "QDialog#shortcutsHelpDialog {"
        "    background: #071728;"
        "    color: #d8e0ea;"
        "    border: 1px solid #173b60;"
        "}"
        "QWidget#dialogTitleBar {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1b5d92, stop:0.42 #15466f, stop:1 #0b2239);"
        "    border-bottom: 1px solid #06111c;"
        "}"
        "QLabel#dialogTitleLabel {"
        "    color: #f2f7fb;"
        "    font-size: 15px;"
        "    font-weight: 700;"
        "}"
        "QPushButton#dialogCloseButton {"
        "    background: transparent;"
        "    border: none;"
        "    min-width: 42px;"
        "    max-width: 42px;"
        "    min-height: 42px;"
        "    max-height: 42px;"
        "}"
        "QPushButton#dialogCloseButton:hover {"
        "    background: rgba(255, 255, 255, 0.12);"
        "}"
        "QPushButton#dialogCloseButton:pressed {"
        "    background: rgba(0, 0, 0, 0.2);"
        "}"
        "QTableWidget {"
        "    background: #091523;"
        "    color: #d8e7f6;"
        "    border: 1px solid #14324f;"
        "    border-radius: 8px;"
        "    gridline-color: rgba(131, 215, 255, 0.08);"
        "    outline: none;"
        "    selection-background-color: #1b5f87;"
        "    selection-color: #ffffff;"
        "}"
        "QHeaderView::section {"
        "    background: #0f253d;"
        "    color: #eef4fa;"
        "    border: none;"
        "    border-bottom: 1px solid #173b60;"
        "    padding: 8px 10px;"
        "    font-weight: 700;"
        "}"
        "QTableWidget::item {"
        "    padding: 8px 10px;"
        "    border: none;"
        "}"
        "QTableWidget::item:selected {"
        "    background: #1b5f87;"
        "    color: #ffffff;"
        "}"
        "QScrollBar:vertical {"
        "    background: transparent;"
        "    width: 10px;"
        "    margin: 4px 2px 4px 0;"
        "}"
        "QScrollBar::handle:vertical {"
        "    background: rgba(131, 215, 255, 0.38);"
        "    border-radius: 4px;"
        "    min-height: 28px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "    background: rgba(131, 215, 255, 0.56);"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "    height: 0;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "    background: transparent;"
        "}"
        "QPushButton#dialogActionButton {"
        "    background: rgba(255, 255, 255, 0.06);"
        "    color: #f5fbff;"
        "    border: none;"
        "    border-radius: 8px;"
        "    min-width: 84px;"
        "    min-height: 38px;"
        "    padding: 0 14px;"
        "}"
        "QPushButton#dialogActionButton:hover {"
        "    background: rgba(255, 255, 255, 0.12);"
        "}"
        "QPushButton#dialogActionButton:pressed {"
        "    background: rgba(8, 29, 49, 0.9);"
        "}");

    auto *main_layout = new QVBoxLayout(&dialog);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    auto *title_bar = new QWidget(&dialog);
    title_bar->setObjectName("dialogTitleBar");
    title_bar->setFixedHeight(42);
    auto *title_layout = new QHBoxLayout(title_bar);
    title_layout->setContentsMargins(14, 0, 0, 0);
    title_layout->setSpacing(0);

    auto *title_label = new QLabel("快捷键说明", title_bar);
    title_label->setObjectName("dialogTitleLabel");

    auto *btn_close = new QPushButton(QIcon(":/icons/title-close.svg"), QString(), title_bar);
    btn_close->setObjectName("dialogCloseButton");
    btn_close->setCursor(Qt::PointingHandCursor);
    btn_close->setIconSize(QSize(14, 14));
    btn_close->setToolTip("关闭");

    title_layout->addWidget(title_label);
    title_layout->addStretch(1);
    title_layout->addWidget(btn_close);

    auto *body = new QWidget(&dialog);
    auto *body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(16, 16, 16, 16);
    body_layout->setSpacing(12);

    auto *table = new QTableWidget(11, 2, body);
    table->setHorizontalHeaderLabels(QStringList{"快捷键", "功能"});
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->setShowGrid(true);
    table->setAlternatingRowColors(false);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table->setWordWrap(false);

    const QList<QPair<QString, QString>> shortcut_items = {
        {"Ctrl+O", "打开文件"},
        {"Ctrl+H", "最近播放"},
        {"Ctrl+Shift+S", "截图"},
        {"Ctrl+L", "顺播开关"},
        {"Ctrl+I", "媒体信息浮层"},
        {"?", "显示快捷键说明"},
        {"Space", "播放/暂停"},
        {"Left / Right", "快退 / 快进 5 秒"},
        {"Up / Down", "音量加 / 减"},
        {"F / F11", "切换全屏"},
        {"Esc", "退出全屏"}};

    for (int row = 0; row < shortcut_items.size(); ++row)
    {
        auto *shortcut_item = new QTableWidgetItem(shortcut_items[row].first);
        auto *action_item = new QTableWidgetItem(shortcut_items[row].second);
        shortcut_item->setTextAlignment(Qt::AlignCenter);
        action_item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        table->setItem(row, 0, shortcut_item);
        table->setItem(row, 1, action_item);
        table->setRowHeight(row, 34);
    }

    auto *button_row = new QWidget(body);
    auto *button_layout = new QHBoxLayout(button_row);
    button_layout->setContentsMargins(0, 0, 0, 0);
    button_layout->setSpacing(0);

    auto *btn_ok = new QPushButton("关闭", button_row);
    btn_ok->setObjectName("dialogActionButton");
    btn_ok->setCursor(Qt::PointingHandCursor);

    button_layout->addStretch(1);
    button_layout->addWidget(btn_ok);

    body_layout->addWidget(table, 1);
    body_layout->addWidget(button_row);

    main_layout->addWidget(title_bar);
    main_layout->addWidget(body, 1);

    connect(btn_close, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(btn_ok, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog_parent != nullptr)
    {
        dialog.move(dialog_parent->frameGeometry().center() - dialog.rect().center());
    }

    dialog.exec();
}

void main_window::show_open_media_menu()
{
    if (btn_open_media_ == nullptr || open_media_menu_ == nullptr)
    {
        return;
    }

    open_media_menu_->popup(btn_open_media_->mapToGlobal(QPoint(0, btn_open_media_->height())));
}

void main_window::show_playlist_manage_menu()
{
    if (btn_playlist_manage_ == nullptr || playlist_manage_menu_ == nullptr)
    {
        return;
    }

    playlist_manage_menu_->popup(btn_playlist_manage_->mapToGlobal(QPoint(0, btn_playlist_manage_->height())));
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

QString main_window::selected_media_target_playlist_id() const
{
    if (playlist_view_ != nullptr)
    {
        QTreeWidgetItem *current_item = playlist_view_->currentItem();
        if (is_playlist_item(current_item) || is_playlist_file_item(current_item))
        {
            const QString playlist_id = playlist_id_for_item(current_item);
            if (!playlist_id.isEmpty())
            {
                return playlist_id;
            }
        }

        const QList<QTreeWidgetItem *> selected_items = playlist_view_->selectedItems();
        for (QTreeWidgetItem *item : selected_items)
        {
            if (is_playlist_item(item) || is_playlist_file_item(item))
            {
                const QString playlist_id = playlist_id_for_item(item);
                if (!playlist_id.isEmpty())
                {
                    return playlist_id;
                }
            }
        }
    }

    return active_playlist_id();
}

void main_window::show_playlist_context_menu(const QPoint &position)
{
    if (playlist_view_ == nullptr)
    {
        return;
    }

    QTreeWidgetItem *item = playlist_view_->itemAt(position);
    if (!is_playlist_item(item))
    {
        return;
    }

    playlist_view_->setCurrentItem(item);

    const QString playlist_id = playlist_id_for_item(item);
    if (playlist_id.isEmpty())
    {
        return;
    }

    QMenu menu(this);
    menu.setStyleSheet(popup_menu_stylesheet());
    QAction *open_file_action = menu.addAction("打开文件到该播放列表");
    QAction *open_folder_action = menu.addAction("打开文件夹到该播放列表");
    QAction *import_playlist_action = menu.addAction("导入播放列表到该播放列表");

    connect(open_file_action,
            &QAction::triggered,
            this,
            [this, playlist_id]()
            {
                open_files_into_playlist(playlist_id);
            });
    connect(open_folder_action,
            &QAction::triggered,
            this,
            [this, playlist_id]()
            {
                open_folder_into_playlist(playlist_id);
            });
    connect(import_playlist_action,
            &QAction::triggered,
            this,
            [this, playlist_id]()
            {
                import_playlist_into_playlist(playlist_id);
            });

    menu.exec(playlist_view_->viewport()->mapToGlobal(position));
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

    update_media_info_overlay();
}

void main_window::set_media_title_text(const QString &text)
{
    media_title_full_text_ = text.trimmed();
    media_title_scroll_offset_ = 0;
    update_media_title_text();
}

void main_window::update_media_title_text()
{
    if (lbl_media_title_ == nullptr)
    {
        return;
    }

    if (media_title_full_text_.isEmpty())
    {
        if (title_scroll_timer_ != nullptr)
        {
            title_scroll_timer_->stop();
        }
        lbl_media_title_->clear();
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

    QSet<QString> collapsed_playlist_ids;
    for (int i = 0; i < playlist_view_->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem *item = playlist_view_->topLevelItem(i);
        if (is_playlist_item(item) && !item->isExpanded())
        {
            collapsed_playlist_ids.insert(playlist_id_for_item(item));
        }
    }

    QSignalBlocker blocker(playlist_view_);
    playlist_view_->clear();

    const QString active_id = active_playlist_id();
    const bool has_playing_item = playing_ && !current_playback_playlist_id_.isEmpty() && current_playback_row_ >= 0;
    const QIcon media_file_icon(":/icons/open-media.svg");
    const QIcon playing_icon(":/icons/play.svg");
    const QBrush playlist_brush(QColor("#eef8ff"));
    const QBrush playing_brush(QColor("#83d7ff"));
    QTreeWidgetItem *current_item = nullptr;
    for (const playlist_entry &entry : playlist_store_.playlists())
    {
        auto *playlist_item = new QTreeWidgetItem(playlist_view_);
        playlist_item->setText(0, entry.name);
        playlist_item->setData(0, k_playlist_item_type_role, k_playlist_type);
        playlist_item->setData(0, k_playlist_id_role, entry.id);
        QFont playlist_font = playlist_item->font(0);
        playlist_font.setBold(true);
        playlist_item->setFont(0, playlist_font);
        playlist_item->setForeground(0, playlist_brush);
        playlist_item->setExpanded(!collapsed_playlist_ids.contains(entry.id));
        update_playlist_item_icon(playlist_item);

        for (int row = 0; row < entry.paths.size(); ++row)
        {
            const QString &path = entry.paths[row];
            auto *file_item = new QTreeWidgetItem(playlist_item);
            file_item->setText(0, QFileInfo(path).fileName());
            file_item->setIcon(0, media_file_icon);
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

    set_playlist_scrollbar_visible(playlist_scrollbar_visible_ && is_cursor_in_playlist_panel());
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

bool main_window::is_drop_region_object(const QObject *watched) const
{
    if (watched == nullptr)
    {
        return false;
    }

    if (watched == this || watched == video_fullscreen_window_)
    {
        return true;
    }

    const auto *widget = qobject_cast<const QWidget *>(watched);
    if (widget == nullptr)
    {
        return false;
    }

    if (isAncestorOf(const_cast<QWidget *>(widget)))
    {
        return true;
    }

    return video_fullscreen_window_ != nullptr && video_fullscreen_window_->isAncestorOf(const_cast<QWidget *>(widget));
}

bool main_window::handle_file_drop(QObject *watched, QEvent *event)
{
    if (!is_drop_region_object(watched) || event == nullptr)
    {
        return false;
    }

    if (event->type() == QEvent::DragEnter)
    {
        auto *drag_enter_event = static_cast<QDragEnterEvent *>(event);
        const QMimeData *mime_data = drag_enter_event->mimeData();
        if (mime_data == nullptr || local_media_files_from_urls(mime_data->urls()).isEmpty())
        {
            return false;
        }

        drag_enter_event->acceptProposedAction();
        return true;
    }

    if (event->type() == QEvent::DragMove)
    {
        auto *drag_move_event = static_cast<QDragMoveEvent *>(event);
        const QMimeData *mime_data = drag_move_event->mimeData();
        if (mime_data == nullptr || local_media_files_from_urls(mime_data->urls()).isEmpty())
        {
            return false;
        }

        drag_move_event->acceptProposedAction();
        return true;
    }

    if (event->type() != QEvent::Drop)
    {
        return false;
    }

    auto *drop_event = static_cast<QDropEvent *>(event);
    const QMimeData *mime_data = drop_event->mimeData();
    if (mime_data == nullptr)
    {
        return false;
    }

    const QStringList files = local_media_files_from_urls(mime_data->urls());
    if (files.isEmpty())
    {
        return false;
    }

    LOG_INFO("media files dropped count {}", files.size());
    open_files_into_playlist(active_playlist_id(), files);
    drop_event->acceptProposedAction();
    return true;
}

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

void main_window::record_playback_history_open(const QString &path)
{
    if (path.isEmpty())
    {
        return;
    }

    const QString normalized_path = normalize_media_path(path);
    const QString entry_group = playback_history_entry_group_key(normalized_path);
    const QString title = QFileInfo(normalized_path).fileName();

    QSettings settings(k_settings_org, k_settings_app);
    touch_playback_history_order(settings, normalized_path);
    settings.setValue(entry_group + "/path", normalized_path);
    settings.setValue(entry_group + "/title", title);
    settings.setValue(entry_group + "/duration", static_cast<int>(duration_));
    settings.setValue(entry_group + "/last_played_at", QDateTime::currentDateTime().toString(Qt::ISODate));
}

void main_window::save_playback_history_entry(int current_second)
{
    if (current_media_path_.isEmpty())
    {
        return;
    }

    const QString normalized_path = normalize_media_path(current_media_path_);
    const QString entry_group = playback_history_entry_group_key(normalized_path);
    const QString title = QFileInfo(normalized_path).fileName();

    QSettings settings(k_settings_org, k_settings_app);
    settings.setValue(entry_group + "/path", normalized_path);
    settings.setValue(entry_group + "/title", title);
    settings.setValue(entry_group + "/duration", static_cast<int>(duration_));
    settings.setValue(entry_group + "/position", current_second);
    settings.setValue(entry_group + "/last_played_at", QDateTime::currentDateTime().toString(Qt::ISODate));
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
    save_playback_history_entry(current_second);
}

void main_window::restore_playback_progress(const QString &path, bool allow_prompt)
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

    if (saved_second < k_resume_prompt_minimum_second)
    {
        return;
    }

    if (duration_ > 1.0 && static_cast<double>(saved_second) >= duration_ - k_resume_prompt_near_end_margin_second)
    {
        settings.setValue(playback_position_key(path), 0);
        settings.setValue(playback_history_entry_group_key(path) + "/position", 0);
        return;
    }

    if (!allow_prompt)
    {
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
        video_fullscreen_window_->setAcceptDrops(true);
        video_fullscreen_window_->installEventFilter(this);
        install_playback_shortcuts(video_fullscreen_window_);

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

    video_fullscreen_window_->showFullScreen();
    video_widget_->show();
    this->hide();
    video_fullscreen_window_->activateWindow();
    video_fullscreen_window_->raise();
    video_fullscreen_window_->setFocus();
    update_media_info_overlay();
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
    update_media_info_overlay();
}

void main_window::on_open_file()
{
    open_files_into_playlist(selected_media_target_playlist_id());
}

void main_window::on_open_folder()
{
    open_folder_into_playlist(selected_media_target_playlist_id());
}

void main_window::open_folder_into_playlist(const QString &playlist_id)
{
    QWidget *dialog_parent = this;
    if (is_video_fullscreen() && video_fullscreen_window_ != nullptr)
    {
        dialog_parent = video_fullscreen_window_;
    }

    const QString directory = QFileDialog::getExistingDirectory(dialog_parent, "打开媒体文件夹");
    if (directory.isEmpty())
    {
        LOG_INFO("open folder cancelled");
        return;
    }

    const QStringList files = collect_media_files_from_directory(directory);
    if (files.isEmpty())
    {
        LOG_INFO("open folder found no media files");
        return;
    }

    LOG_INFO("open folder selected {} media count {}", directory.toStdString(), files.size());
    open_files_into_playlist(playlist_id, files);
}

void main_window::on_import_playlist()
{
    import_playlist_into_playlist(selected_media_target_playlist_id());
}

void main_window::import_playlist_into_playlist(const QString &playlist_id)
{
    QWidget *dialog_parent = this;
    if (is_video_fullscreen() && video_fullscreen_window_ != nullptr)
    {
        dialog_parent = video_fullscreen_window_;
    }

    const QString playlist_path = QFileDialog::getOpenFileName(
        dialog_parent,
        "导入播放列表",
        "",
        "Playlist Files (*.m3u *.m3u8)");
    if (playlist_path.isEmpty())
    {
        LOG_INFO("import playlist cancelled");
        return;
    }

    const QStringList files = load_playlist_file_paths(playlist_path);
    if (files.isEmpty())
    {
        LOG_INFO("import playlist found no valid media files");
        return;
    }

    LOG_INFO("import playlist selected {} media count {}", playlist_path.toStdString(), files.size());
    open_files_into_playlist(playlist_id, files);
}

void main_window::on_export_playlist()
{
    const playlist_entry *playlist = playlist_store_.playlist_by_id(active_playlist_id());
    if (playlist == nullptr || playlist->paths.isEmpty())
    {
        LOG_INFO("export playlist skipped because current playlist is empty");
        return;
    }

    QWidget *dialog_parent = this;
    if (is_video_fullscreen() && video_fullscreen_window_ != nullptr)
    {
        dialog_parent = video_fullscreen_window_;
    }

    QString default_name = playlist->name.trimmed();
    if (default_name.isEmpty())
    {
        default_name = "playlist";
    }

    QString output_path = QFileDialog::getSaveFileName(
        dialog_parent,
        "导出播放列表",
        default_name + ".m3u8",
        "Playlist Files (*.m3u *.m3u8)");
    if (output_path.isEmpty())
    {
        LOG_INFO("export playlist cancelled");
        return;
    }

    if (!output_path.endsWith(".m3u", Qt::CaseInsensitive) && !output_path.endsWith(".m3u8", Qt::CaseInsensitive))
    {
        output_path += ".m3u8";
    }

    QFile file(output_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        LOG_ERROR("failed to open playlist file for writing {}", output_path.toStdString());
        return;
    }

    file.write("#EXTM3U\n");
    for (const QString &path : playlist->paths)
    {
        file.write(path.toUtf8());
        file.write("\n");
    }

    LOG_INFO("playlist exported {} items {}", output_path.toStdString(), playlist->paths.size());
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
        media_file_dialog_filter());
    if (filenames.isEmpty())
    {
        LOG_INFO("open file cancelled");
        return;
    }

    open_files_into_playlist(playlist_id, filenames);
}

void main_window::open_files_into_playlist(const QString &playlist_id, const QStringList &filenames)
{
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
        play_playlist_item(target_playlist_id, target_row, true);
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
        play_selected_playlist_item();
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
        btn_playlist_manage_->setToolTip("播放列表操作");
    }
}

void main_window::update_control_layout_mode()
{
    int layout_mode = 0;
    if (width() < k_narrow_control_bar_width)
    {
        layout_mode = 2;
    }
    else if (width() < k_compact_control_bar_width)
    {
        layout_mode = 1;
    }

    if (primary_control_row_layout_ == nullptr || secondary_control_row_layout_ == nullptr || tertiary_control_row_layout_ == nullptr)
    {
        return;
    }

    if (control_layout_mode_ == layout_mode && primary_control_row_layout_->count() > 0)
    {
        return;
    }

    control_layout_mode_ = layout_mode;
    rebuild_control_rows(layout_mode);
}

void main_window::rebuild_control_rows(int layout_mode)
{
    if (primary_control_row_layout_ == nullptr || secondary_control_row_layout_ == nullptr || secondary_control_row_widget_ == nullptr ||
        tertiary_control_row_layout_ == nullptr || tertiary_control_row_widget_ == nullptr || control_panel_ == nullptr || lbl_time_ == nullptr)
    {
        return;
    }

    while (primary_control_row_layout_->count() > 0)
    {
        delete primary_control_row_layout_->takeAt(0);
    }
    while (secondary_control_row_layout_->count() > 0)
    {
        delete secondary_control_row_layout_->takeAt(0);
    }
    while (tertiary_control_row_layout_->count() > 0)
    {
        delete tertiary_control_row_layout_->takeAt(0);
    }

    if (layout_mode == 2)
    {
        control_panel_->setFixedHeight(182);
        lbl_time_->setMinimumWidth(0);

        primary_control_row_layout_->addStretch(1);
        primary_control_row_layout_->addWidget(btn_stop_);
        primary_control_row_layout_->addWidget(btn_backward_);
        primary_control_row_layout_->addWidget(btn_play_pause_);
        primary_control_row_layout_->addWidget(btn_forward_);
        primary_control_row_layout_->addStretch(1);

        secondary_control_row_layout_->addWidget(lbl_time_, 1);
        secondary_control_row_layout_->addSpacing(8);
        secondary_control_row_layout_->addWidget(btn_audio_only_);

        tertiary_control_row_layout_->addWidget(btn_playback_rate_);
        tertiary_control_row_layout_->addWidget(lbl_vol_icon_low_);
        tertiary_control_row_layout_->addWidget(volume_meter_);
        tertiary_control_row_layout_->addStretch(1);
        tertiary_control_row_layout_->addWidget(btn_open_media_);
        tertiary_control_row_layout_->addWidget(btn_video_fullscreen_);
        tertiary_control_row_layout_->addWidget(btn_playlist_);

        secondary_control_row_widget_->show();
        tertiary_control_row_widget_->show();
        return;
    }

    if (layout_mode == 1)
    {
        control_panel_->setFixedHeight(140);
        lbl_time_->setMinimumWidth(180);

        primary_control_row_layout_->addStretch(1);
        primary_control_row_layout_->addWidget(btn_stop_);
        primary_control_row_layout_->addWidget(btn_backward_);
        primary_control_row_layout_->addWidget(btn_play_pause_);
        primary_control_row_layout_->addWidget(btn_forward_);
        primary_control_row_layout_->addWidget(btn_audio_only_);
        primary_control_row_layout_->addStretch(1);

        secondary_control_row_layout_->addWidget(lbl_time_);
        secondary_control_row_layout_->addStretch(1);
        secondary_control_row_layout_->addWidget(btn_playback_rate_);
        secondary_control_row_layout_->addWidget(lbl_vol_icon_low_);
        secondary_control_row_layout_->addWidget(volume_meter_);
        secondary_control_row_layout_->addSpacing(12);
        secondary_control_row_layout_->addWidget(btn_open_media_);
        secondary_control_row_layout_->addWidget(btn_video_fullscreen_);
        secondary_control_row_layout_->addWidget(btn_playlist_);
        secondary_control_row_widget_->show();
        tertiary_control_row_widget_->hide();
        return;
    }

    control_panel_->setFixedHeight(104);
    lbl_time_->setMinimumWidth(210);

    primary_control_row_layout_->addWidget(lbl_time_);
    primary_control_row_layout_->addStretch(1);
    primary_control_row_layout_->addWidget(btn_stop_);
    primary_control_row_layout_->addWidget(btn_backward_);
    primary_control_row_layout_->addWidget(btn_play_pause_);
    primary_control_row_layout_->addWidget(btn_forward_);
    primary_control_row_layout_->addWidget(btn_audio_only_);
    primary_control_row_layout_->addStretch(1);
    primary_control_row_layout_->addWidget(btn_playback_rate_);
    primary_control_row_layout_->addWidget(lbl_vol_icon_low_);
    primary_control_row_layout_->addWidget(volume_meter_);
    primary_control_row_layout_->addSpacing(12);
    primary_control_row_layout_->addWidget(btn_open_media_);
    primary_control_row_layout_->addWidget(btn_video_fullscreen_);
    primary_control_row_layout_->addWidget(btn_playlist_);
    secondary_control_row_widget_->hide();
    tertiary_control_row_widget_->hide();
}

void main_window::play_selected_playlist_item()
{
    if (playlist_view_ != nullptr && playlist_view_->currentItem() != nullptr)
    {
        QTreeWidgetItem *current_item = playlist_view_->currentItem();
        if (is_playlist_file_item(current_item))
        {
            play_playlist_item(playlist_id_for_item(current_item), playlist_row_for_item(current_item), true);
            return;
        }

        if (is_playlist_item(current_item))
        {
            const QString playlist_id = playlist_id_for_item(current_item);
            const playlist_entry *entry = playlist_store_.playlist_by_id(playlist_id);
            if (entry != nullptr && !entry->paths.isEmpty())
            {
                const int row = entry->current_row >= 0 ? entry->current_row : 0;
                play_playlist_item(playlist_id, row, true);
                return;
            }
        }
    }

    const QString playlist_id = active_playlist_id();
    const playlist_entry *entry = playlist_store_.playlist_by_id(playlist_id);
    if (entry == nullptr || entry->paths.isEmpty())
    {
        return;
    }

    const int row = entry->current_row >= 0 ? entry->current_row : 0;
    play_playlist_item(playlist_id, row, true);
}

void main_window::update_playlist_item_icon(QTreeWidgetItem *item)
{
    if (!is_playlist_item(item))
    {
        return;
    }

    static const QIcon collapsed_icon(":/icons/playlist-folder.svg");
    static const QIcon expanded_icon(":/icons/playlist-folder-open.svg");
    item->setIcon(0, item->isExpanded() ? expanded_icon : collapsed_icon);
}

void main_window::on_toggle_playlist()
{
    const bool visible = playlist_panel_->isVisible();
    playlist_panel_->setVisible(!visible);
    if (visible)
    {
        if (playlist_scrollbar_hide_timer_ != nullptr)
        {
            playlist_scrollbar_hide_timer_->stop();
        }
        set_playlist_scrollbar_visible(false);
    }
    else
    {
        set_playlist_scrollbar_visible(is_cursor_in_playlist_panel());
    }
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
        play_playlist_item(playlist_id, row - 1, false);
    }
}

void main_window::on_play_next()
{
    const QString playlist_id = playing_ ? playback_playlist_id() : active_playlist_id();
    const playlist_entry *entry = playlist_store_.playlist_by_id(playlist_id);
    const int row = playing_ ? playback_playlist_row() : playlist_store_.current_row(playlist_id);
    if (entry != nullptr && row >= 0 && row + 1 < entry->paths.size())
    {
        play_playlist_item(playlist_id, row + 1, false);
    }
}

void main_window::on_playlist_item_activated(QTreeWidgetItem *item, int)
{
    if (is_playlist_file_item(item))
    {
        play_playlist_item(playlist_id_for_item(item), playlist_row_for_item(item), true);
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
    update_media_info_overlay();
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

void main_window::play_playlist_item(const QString &playlist_id, int row, bool allow_resume_prompt)
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
    record_playback_history_open(path);
    playlist_store_.set_active_playlist(playlist_id);
    playlist_store_.set_current_row(playlist_id, row);
    refresh_playlist_view();
    update_playlist_buttons();
    save_playlist_state();
    restore_playback_progress(path, allow_resume_prompt);

    const QString display_name = QFileInfo(path).fileName();
    this->setWindowTitle(display_name + " - 视频播放器");
    if (video_fullscreen_window_ != nullptr)
    {
        video_fullscreen_window_->setWindowTitle(this->windowTitle());
        video_fullscreen_window_->setWindowIcon(this->windowIcon());
    }
    set_media_title_text(display_name);
    update_media_info_overlay();
}

void main_window::play_playlist_row(int row, bool allow_resume_prompt) { play_playlist_item(active_playlist_id(), row, allow_resume_prompt); }

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
                play_playlist_item(playlist_id, next_row, false);
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

    ++playback_generation_;

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
    this->setWindowTitle("视频播放器");
    if (video_fullscreen_window_ != nullptr)
    {
        video_fullscreen_window_->setWindowTitle(this->windowTitle());
    }
    set_media_title_text(QString());
    refresh_playlist_view();
    update_playlist_buttons();
    update_fullscreen_button();
    update_screenshot_button();
    update_media_info_overlay();
    LOG_INFO("stop play finished");
}

bool main_window::start_play(const std::string &filepath)
{
    LOG_INFO("starting play for file {}", filepath);
    const uint64_t playback_generation = ++playback_generation_;
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

        connect(sync_thread_.get(),
                &video_sync_thread::frame_ready,
                this,
                [this, playback_generation](std::shared_ptr<media_frame> frame)
                {
                    if (playback_generation != playback_generation_)
                    {
                        return;
                    }

                    on_video_frame_ready(std::move(frame));
                });

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
    update_media_info_overlay();

    LOG_INFO("play started successfully");

    return true;
}
