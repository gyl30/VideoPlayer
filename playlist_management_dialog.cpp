#include <QFileInfo>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>
#include "playlist_name_dialog.h"
#include "playlist_management_dialog.h"

namespace
{
QListWidgetItem *create_playlist_item(const playlist_entry &entry)
{
    auto *item = new QListWidgetItem(QString("%1 [%2]").arg(entry.name).arg(entry.paths.size()));
    item->setData(Qt::UserRole, entry.id);
    item->setToolTip(entry.name);
    return item;
}
}  // namespace

playlist_management_dialog::playlist_management_dialog(const playlist_store &store, QWidget *parent)
    : QDialog(parent), temp_store_(store)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    setWindowTitle("管理播放列表");
    resize(820, 520);
    setObjectName("playlistManagementDialog");
    setStyleSheet(
        "QDialog#playlistManagementDialog {"
        "    background: #071b30;"
        "    color: #d8e0ea;"
        "    border: 1px solid #16385d;"
        "}"
        "QWidget#dialogTitleBar {"
        "    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #1c6097, stop:0.48 #143d65, stop:1 #0a2036);"
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
        "    border-radius: 0;"
        "    min-width: 42px;"
        "    max-width: 42px;"
        "    min-height: 42px;"
        "    max-height: 42px;"
        "    padding: 0;"
        "}"
        "QPushButton#dialogCloseButton:hover {"
        "    background: rgba(255, 255, 255, 0.12);"
        "}"
        "QPushButton#dialogCloseButton:pressed {"
        "    background: rgba(0, 0, 0, 0.2);"
        "}"
        "QWidget#playlistSection {"
        "    background: #0b1929;"
        "    border: 1px solid #16385d;"
        "    border-radius: 8px;"
        "}"
        "QWidget#playlistActionPanel {"
        "    background: transparent;"
        "}"
        "QLabel[sectionTitle=\"true\"] {"
        "    color: #eef4fa;"
        "    font-size: 14px;"
        "    font-weight: 600;"
        "}"
        "QListWidget {"
        "    background: transparent;"
        "    color: #d8e7f6;"
        "    border: 1px solid #16385d;"
        "    border-radius: 6px;"
        "    outline: none;"
        "    padding: 4px;"
        "}"
        "QListWidget::item {"
        "    min-height: 30px;"
        "    padding: 4px 8px;"
        "    border-radius: 4px;"
        "}"
        "QListWidget::item:hover {"
        "    background: #1b2631;"
        "    color: #f3f8fc;"
        "}"
        "QListWidget::item:selected {"
        "    background: #174a68;"
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
        "QScrollBar:horizontal {"
        "    background: transparent;"
        "    height: 0;"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
        "    width: 0;"
        "}"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
        "    background: transparent;"
        "}"
        "QPushButton {"
        "    background: transparent;"
        "    color: #f5fbff;"
        "    border: none;"
        "    border-radius: 4px;"
        "    min-height: 38px;"
        "    padding: 0 12px;"
        "}"
        "QPushButton:hover {"
        "    background: rgba(255, 255, 255, 0.12);"
        "    color: #ffffff;"
        "}"
        "QPushButton:pressed {"
        "    background: rgba(8, 29, 49, 0.9);"
        "}"
        "QPushButton:disabled {"
        "    color: rgba(245, 251, 255, 0.35);"
        "}"
    );

    setup_ui();
    setup_connections();
    populate_playlist_lists();
}

const playlist_store &playlist_management_dialog::result_store() const { return temp_store_; }

bool playlist_management_dialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != title_bar_)
    {
        return QDialog::eventFilter(watched, event);
    }

    switch (event->type())
    {
        case QEvent::MouseButtonPress:
        {
            auto *mouse_event = static_cast<QMouseEvent *>(event);
            if (mouse_event->button() == Qt::LeftButton)
            {
                dragging_title_bar_ = true;
                drag_offset_ = mouse_event->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
            break;
        }
        case QEvent::MouseMove:
        {
            if (!dragging_title_bar_)
            {
                break;
            }
            auto *mouse_event = static_cast<QMouseEvent *>(event);
            if (!(mouse_event->buttons() & Qt::LeftButton))
            {
                dragging_title_bar_ = false;
                break;
            }
            move(mouse_event->globalPosition().toPoint() - drag_offset_);
            return true;
        }
        case QEvent::MouseButtonRelease:
        {
            auto *mouse_event = static_cast<QMouseEvent *>(event);
            if (mouse_event->button() == Qt::LeftButton)
            {
                dragging_title_bar_ = false;
                return true;
            }
            break;
        }
        default:
            break;
    }

    return QDialog::eventFilter(watched, event);
}

void playlist_management_dialog::on_source_playlist_changed()
{
    update_song_list(source_playlists_list_, source_songs_list_, true);
    update_action_buttons();
}

void playlist_management_dialog::on_target_playlist_changed()
{
    update_song_list(target_playlists_list_, target_songs_list_, false);
    update_action_buttons();
}

void playlist_management_dialog::on_source_selection_changed() { update_action_buttons(); }

void playlist_management_dialog::on_create_playlist()
{
    bool accepted = false;
    const QString name = playlist_name_dialog::get_text(this, "新建播放列表", "播放列表名称", "创建", "", &accepted);
    if (!accepted)
    {
        return;
    }

    const QString playlist_id = temp_store_.create_playlist(name);
    temp_store_.set_active_playlist(playlist_id);
    populate_playlist_lists();
}

void playlist_management_dialog::on_rename_playlist()
{
    const QString playlist_id = current_playlist_id(source_playlists_list_);
    const playlist_entry *entry = temp_store_.playlist_by_id(playlist_id);
    if (entry == nullptr)
    {
        return;
    }

    bool accepted = false;
    const QString name = playlist_name_dialog::get_text(this, "重命名播放列表", "播放列表名称", "保存", entry->name, &accepted);
    if (!accepted || !temp_store_.rename_playlist(playlist_id, name))
    {
        return;
    }

    populate_playlist_lists();
}

void playlist_management_dialog::on_copy_rows()
{
    const QString source_id = current_playlist_id(source_playlists_list_);
    const QString target_id = current_playlist_id(target_playlists_list_);
    const QList<int> rows = selected_source_rows();
    if (rows.isEmpty() || source_id.isEmpty() || target_id.isEmpty())
    {
        return;
    }

    if (temp_store_.copy_rows(source_id, rows, target_id))
    {
        populate_playlist_lists();
    }
}

void playlist_management_dialog::on_move_rows()
{
    const QString source_id = current_playlist_id(source_playlists_list_);
    const QString target_id = current_playlist_id(target_playlists_list_);
    const QList<int> rows = selected_source_rows();
    if (rows.isEmpty() || source_id.isEmpty() || target_id.isEmpty())
    {
        return;
    }

    if (temp_store_.move_rows(source_id, rows, target_id))
    {
        populate_playlist_lists();
    }
}

void playlist_management_dialog::on_remove_rows()
{
    const QString playlist_id = current_playlist_id(source_playlists_list_);
    const QList<int> rows = selected_source_rows();
    if (rows.isEmpty() || playlist_id.isEmpty())
    {
        return;
    }

    if (temp_store_.remove_rows(playlist_id, rows))
    {
        populate_playlist_lists();
    }
}

void playlist_management_dialog::setup_ui()
{
    auto *root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->setSpacing(0);

    title_bar_ = new QWidget(this);
    title_bar_->setObjectName("dialogTitleBar");
    title_bar_->setFixedHeight(42);
    title_bar_->installEventFilter(this);

    auto *title_layout = new QHBoxLayout(title_bar_);
    title_layout->setContentsMargins(14, 0, 0, 0);
    title_layout->setSpacing(0);

    auto *title_label = new QLabel("管理播放列表", title_bar_);
    title_label->setObjectName("dialogTitleLabel");

    auto *btn_close = new QPushButton(QIcon(":/icons/title-close.svg"), QString(), title_bar_);
    btn_close->setObjectName("dialogCloseButton");
    btn_close->setCursor(Qt::PointingHandCursor);
    btn_close->setIconSize(QSize(14, 14));
    btn_close->setToolTip("关闭");

    title_layout->addWidget(title_label);
    title_layout->addStretch(1);
    title_layout->addWidget(btn_close);

    auto *body = new QWidget(this);
    auto *main_layout = new QHBoxLayout(body);
    main_layout->setContentsMargins(12, 12, 12, 12);
    main_layout->setSpacing(10);

    auto *source_panel = new QWidget(this);
    source_panel->setObjectName("playlistSection");
    auto *source_layout = new QVBoxLayout(source_panel);
    source_layout->setContentsMargins(12, 12, 12, 12);
    source_layout->setSpacing(8);
    auto *source_playlist_label = new QLabel("源播放列表", source_panel);
    source_playlist_label->setProperty("sectionTitle", true);
    source_layout->addWidget(source_playlist_label);

    source_playlists_list_ = new QListWidget(source_panel);
    source_layout->addWidget(source_playlists_list_);

    auto *playlist_button_row = new QWidget(source_panel);
    auto *playlist_button_layout = new QHBoxLayout(playlist_button_row);
    playlist_button_layout->setContentsMargins(0, 0, 0, 0);
    playlist_button_layout->setSpacing(8);

    btn_create_playlist_ = new QPushButton("新建播放列表", playlist_button_row);
    btn_rename_playlist_ = new QPushButton("重命名播放列表", playlist_button_row);
    btn_rename_playlist_->setEnabled(false);
    playlist_button_layout->addWidget(btn_create_playlist_);
    playlist_button_layout->addWidget(btn_rename_playlist_);
    source_layout->addWidget(playlist_button_row);

    auto *source_file_label = new QLabel("源文件列表", source_panel);
    source_file_label->setProperty("sectionTitle", true);
    source_layout->addWidget(source_file_label);
    source_songs_list_ = new QListWidget(source_panel);
    source_songs_list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    source_songs_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    source_layout->addWidget(source_songs_list_, 1);

    auto *action_panel = new QWidget(this);
    action_panel->setObjectName("playlistActionPanel");
    auto *action_layout = new QVBoxLayout(action_panel);
    action_layout->setContentsMargins(0, 0, 0, 0);
    action_layout->setSpacing(8);
    action_layout->addStretch();

    btn_copy_ = new QPushButton("复制到目标列表", action_panel);
    btn_move_ = new QPushButton("移动到目标列表", action_panel);
    btn_remove_ = new QPushButton("从源列表移除", action_panel);
    btn_copy_->setEnabled(false);
    btn_move_->setEnabled(false);
    btn_remove_->setEnabled(false);
    action_layout->addWidget(btn_copy_);
    action_layout->addWidget(btn_move_);
    action_layout->addWidget(btn_remove_);
    action_layout->addStretch();

    btn_apply_ = new QPushButton("应用并返回", action_panel);
    btn_cancel_ = new QPushButton("取消", action_panel);
    action_layout->addWidget(btn_apply_);
    action_layout->addWidget(btn_cancel_);

    auto *target_panel = new QWidget(this);
    target_panel->setObjectName("playlistSection");
    auto *target_layout = new QVBoxLayout(target_panel);
    target_layout->setContentsMargins(12, 12, 12, 12);
    target_layout->setSpacing(8);
    auto *target_playlist_label = new QLabel("目标播放列表", target_panel);
    target_playlist_label->setProperty("sectionTitle", true);
    target_layout->addWidget(target_playlist_label);

    target_playlists_list_ = new QListWidget(target_panel);
    target_layout->addWidget(target_playlists_list_);

    auto *target_file_label = new QLabel("目标文件列表", target_panel);
    target_file_label->setProperty("sectionTitle", true);
    target_layout->addWidget(target_file_label);
    target_songs_list_ = new QListWidget(target_panel);
    target_songs_list_->setSelectionMode(QAbstractItemView::NoSelection);
    target_songs_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    target_layout->addWidget(target_songs_list_, 1);

    main_layout->addWidget(source_panel, 2);
    main_layout->addWidget(action_panel, 1);
    main_layout->addWidget(target_panel, 2);

    root_layout->addWidget(title_bar_);
    root_layout->addWidget(body);

    connect(btn_close, &QPushButton::clicked, this, &QDialog::reject);
}

void playlist_management_dialog::setup_connections()
{
    connect(source_playlists_list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *, QListWidgetItem *)
            {
                on_source_playlist_changed();
            });
    connect(source_songs_list_, &QListWidget::itemSelectionChanged, this, &playlist_management_dialog::on_source_selection_changed);
    connect(target_playlists_list_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *, QListWidgetItem *)
            {
                on_target_playlist_changed();
            });
    connect(btn_create_playlist_, &QPushButton::clicked, this, &playlist_management_dialog::on_create_playlist);
    connect(btn_rename_playlist_, &QPushButton::clicked, this, &playlist_management_dialog::on_rename_playlist);
    connect(btn_copy_, &QPushButton::clicked, this, &playlist_management_dialog::on_copy_rows);
    connect(btn_move_, &QPushButton::clicked, this, &playlist_management_dialog::on_move_rows);
    connect(btn_remove_, &QPushButton::clicked, this, &playlist_management_dialog::on_remove_rows);
    connect(btn_apply_, &QPushButton::clicked, this, &QDialog::accept);
    connect(btn_cancel_, &QPushButton::clicked, this, &QDialog::reject);
}

void playlist_management_dialog::populate_playlist_lists()
{
    const QString source_current_id = current_playlist_id(source_playlists_list_);
    const QString target_current_id = current_playlist_id(target_playlists_list_);

    source_playlists_list_->clear();
    target_playlists_list_->clear();

    int source_row = 0;
    int target_row = 0;
    for (int i = 0; i < temp_store_.playlists().size(); ++i)
    {
        const playlist_entry &entry = temp_store_.playlists()[i];
        source_playlists_list_->addItem(create_playlist_item(entry));
        target_playlists_list_->addItem(create_playlist_item(entry));

        if (!source_current_id.isEmpty() && entry.id == source_current_id)
        {
            source_row = i;
        }
        if (!target_current_id.isEmpty() && entry.id == target_current_id)
        {
            target_row = i;
        }
    }

    if (source_playlists_list_->count() > 0)
    {
        source_playlists_list_->setCurrentRow(source_row);
    }
    if (target_playlists_list_->count() > 0)
    {
        target_playlists_list_->setCurrentRow(target_row);
    }
}

void playlist_management_dialog::update_song_list(QListWidget *playlist_list, QListWidget *song_list, bool source_side)
{
    if (playlist_list == nullptr || song_list == nullptr)
    {
        return;
    }

    song_list->clear();
    const QString playlist_id = current_playlist_id(playlist_list);
    const playlist_entry *entry = temp_store_.playlist_by_id(playlist_id);
    if (entry == nullptr)
    {
        return;
    }

    for (const QString &path : entry->paths)
    {
        auto *item = new QListWidgetItem(QFileInfo(path).fileName(), song_list);
        item->setToolTip(path);
        item->setData(Qt::UserRole, path);
    }

    if (source_side)
    {
        update_action_buttons();
    }
}

QString playlist_management_dialog::current_playlist_id(QListWidget *playlist_list) const
{
    if (playlist_list == nullptr || playlist_list->currentItem() == nullptr)
    {
        return {};
    }
    return playlist_list->currentItem()->data(Qt::UserRole).toString();
}

QList<int> playlist_management_dialog::selected_source_rows() const
{
    QList<int> rows;
    if (source_songs_list_ == nullptr)
    {
        return rows;
    }

    const QList<QListWidgetItem *> items = source_songs_list_->selectedItems();
    for (QListWidgetItem *item : items)
    {
        if (item != nullptr)
        {
            rows.append(source_songs_list_->row(item));
        }
    }
    return rows;
}

void playlist_management_dialog::update_action_buttons()
{
    const QString source_id = current_playlist_id(source_playlists_list_);
    const QString target_id = current_playlist_id(target_playlists_list_);
    const bool has_source_playlist = !source_id.isEmpty();
    const bool has_target_playlist = !target_id.isEmpty();
    const bool has_selected_rows = !selected_source_rows().isEmpty();
    const bool different_playlist = has_source_playlist && has_target_playlist && source_id != target_id;

    btn_rename_playlist_->setEnabled(has_source_playlist);
    btn_copy_->setEnabled(has_selected_rows && different_playlist);
    btn_move_->setEnabled(has_selected_rows && different_playlist);
    btn_remove_->setEnabled(has_selected_rows && has_source_playlist);
}
