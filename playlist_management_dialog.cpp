#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
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
    setWindowTitle("管理播放列表");
    resize(820, 520);

    setup_ui();
    setup_connections();
    populate_playlist_lists();
}

const playlist_store &playlist_management_dialog::result_store() const { return temp_store_; }

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
    auto *main_layout = new QHBoxLayout(this);
    main_layout->setContentsMargins(12, 12, 12, 12);
    main_layout->setSpacing(10);

    auto *source_panel = new QWidget(this);
    auto *source_layout = new QVBoxLayout(source_panel);
    source_layout->setContentsMargins(0, 0, 0, 0);
    source_layout->setSpacing(8);
    source_layout->addWidget(new QLabel("源播放列表", source_panel));

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

    source_layout->addWidget(new QLabel("源文件列表", source_panel));
    source_songs_list_ = new QListWidget(source_panel);
    source_songs_list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    source_layout->addWidget(source_songs_list_, 1);

    auto *action_panel = new QWidget(this);
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
    auto *target_layout = new QVBoxLayout(target_panel);
    target_layout->setContentsMargins(0, 0, 0, 0);
    target_layout->setSpacing(8);
    target_layout->addWidget(new QLabel("目标播放列表", target_panel));

    target_playlists_list_ = new QListWidget(target_panel);
    target_layout->addWidget(target_playlists_list_);

    target_layout->addWidget(new QLabel("目标文件列表", target_panel));
    target_songs_list_ = new QListWidget(target_panel);
    target_songs_list_->setSelectionMode(QAbstractItemView::NoSelection);
    target_layout->addWidget(target_songs_list_, 1);

    main_layout->addWidget(source_panel, 2);
    main_layout->addWidget(action_panel, 1);
    main_layout->addWidget(target_panel, 2);
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
