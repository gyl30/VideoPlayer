#include <algorithm>
#include <QUuid>
#include "playlist_store.h"

namespace
{
QString normalize_playlist_name(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty())
    {
        return trimmed;
    }
    return playlist_store::default_playlist_name();
}
}  // namespace

void playlist_store::load(QSettings &settings)
{
    playlists_.clear();
    active_playlist_id_.clear();

    settings.beginGroup("playlists");
    const QStringList order = settings.value("order").toStringList();
    active_playlist_id_ = settings.value("active").toString();

    for (const QString &id : order)
    {
        if (id.isEmpty())
        {
            continue;
        }

        settings.beginGroup(id);
        playlist_entry entry;
        entry.id = id;
        entry.name = normalize_playlist_name(settings.value("name").toString());
        entry.paths = settings.value("paths").toStringList();
        entry.current_row = settings.value("current_row", -1).toInt();
        settings.endGroup();

        if (entry.current_row >= entry.paths.size())
        {
            entry.current_row = entry.paths.isEmpty() ? -1 : static_cast<int>(entry.paths.size() - 1);
        }
        playlists_.append(std::move(entry));
    }
    settings.endGroup();

    if (playlists_.isEmpty())
    {
        playlist_entry migrated_entry;
        migrated_entry.id = create_playlist_id();
        migrated_entry.name = default_playlist_name();
        migrated_entry.paths = settings.value("playlist/paths").toStringList();
        migrated_entry.current_row = settings.value("playlist/currentRow", -1).toInt();
        if (migrated_entry.current_row >= migrated_entry.paths.size())
        {
            migrated_entry.current_row = migrated_entry.paths.isEmpty() ? -1 : static_cast<int>(migrated_entry.paths.size() - 1);
        }
        playlists_.append(std::move(migrated_entry));
    }

    ensure_default_playlist();

    if (find_index_by_id(active_playlist_id_) < 0)
    {
        active_playlist_id_ = playlists_.front().id;
    }
}

void playlist_store::save(QSettings &settings) const
{
    settings.beginGroup("playlists");
    settings.remove("");

    QStringList order;
    for (const playlist_entry &entry : playlists_)
    {
        order.append(entry.id);
        settings.beginGroup(entry.id);
        settings.setValue("name", entry.name);
        settings.setValue("paths", entry.paths);
        settings.setValue("current_row", entry.current_row);
        settings.endGroup();
    }

    settings.setValue("order", order);
    settings.setValue("active", active_playlist_id_);
    settings.endGroup();
}

void playlist_store::ensure_default_playlist()
{
    if (!playlists_.isEmpty())
    {
        return;
    }

    playlist_entry entry;
    entry.id = create_playlist_id();
    entry.name = default_playlist_name();
    playlists_.append(std::move(entry));
    active_playlist_id_ = playlists_.front().id;
}

const QList<playlist_entry> &playlist_store::playlists() const { return playlists_; }

int playlist_store::playlist_count() const { return static_cast<int>(playlists_.size()); }

QString playlist_store::active_playlist_id() const { return active_playlist_id_; }

int playlist_store::active_index() const { return find_index_by_id(active_playlist_id_); }

const playlist_entry *playlist_store::active_playlist() const
{
    const int index = active_index();
    if (index < 0)
    {
        return nullptr;
    }
    return &playlists_[index];
}

const playlist_entry *playlist_store::playlist_by_id(const QString &id) const
{
    const int index = find_index_by_id(id);
    if (index < 0)
    {
        return nullptr;
    }
    return &playlists_[index];
}

int playlist_store::current_row(const QString &id) const
{
    const playlist_entry *entry = playlist_by_id(id);
    if (entry == nullptr)
    {
        return -1;
    }
    return entry->current_row;
}

int playlist_store::index_of_path(const QString &id, const QString &path) const
{
    const playlist_entry *entry = playlist_by_id(id);
    if (entry == nullptr)
    {
        return -1;
    }
    return static_cast<int>(entry->paths.indexOf(path));
}

QString playlist_store::create_playlist(const QString &name)
{
    playlist_entry entry;
    entry.id = create_playlist_id();
    entry.name = normalize_playlist_name(name);
    playlists_.append(std::move(entry));
    if (active_playlist_id_.isEmpty())
    {
        active_playlist_id_ = playlists_.back().id;
    }
    return playlists_.back().id;
}

bool playlist_store::rename_playlist(const QString &id, const QString &name)
{
    const int index = find_index_by_id(id);
    if (index < 0)
    {
        return false;
    }

    playlists_[index].name = normalize_playlist_name(name);
    return true;
}

bool playlist_store::remove_playlist(const QString &id)
{
    if (playlists_.size() <= 1)
    {
        return false;
    }

    const int index = find_index_by_id(id);
    if (index < 0)
    {
        return false;
    }

    playlists_.removeAt(index);
    if (active_playlist_id_ == id)
    {
        active_playlist_id_ = playlists_.front().id;
    }
    return true;
}

bool playlist_store::set_active_playlist(const QString &id)
{
    if (find_index_by_id(id) < 0)
    {
        return false;
    }
    active_playlist_id_ = id;
    return true;
}

bool playlist_store::set_current_row(const QString &id, int row)
{
    const int index = find_index_by_id(id);
    if (index < 0)
    {
        return false;
    }

    const int max_row = static_cast<int>(playlists_[index].paths.size() - 1);
    if (max_row < 0)
    {
        playlists_[index].current_row = -1;
        return true;
    }

    playlists_[index].current_row = std::clamp(row, 0, max_row);
    return true;
}

bool playlist_store::add_path(const QString &id, const QString &path)
{
    const int index = find_index_by_id(id);
    if (index < 0 || path.isEmpty())
    {
        return false;
    }

    if (playlists_[index].paths.contains(path))
    {
        return false;
    }

    playlists_[index].paths.append(path);
    if (playlists_[index].current_row < 0)
    {
        playlists_[index].current_row = 0;
    }
    return true;
}

bool playlist_store::remove_rows(const QString &id, const QList<int> &rows)
{
    const int index = find_index_by_id(id);
    if (index < 0 || rows.isEmpty())
    {
        return false;
    }

    QList<int> sorted_rows = rows;
    std::sort(sorted_rows.begin(), sorted_rows.end(), std::greater<int>());

    bool removed = false;
    for (int row : sorted_rows)
    {
        if (row < 0 || row >= playlists_[index].paths.size())
        {
            continue;
        }
        playlists_[index].paths.removeAt(row);
        removed = true;
    }

    if (!removed)
    {
        return false;
    }

    if (playlists_[index].paths.isEmpty())
    {
        playlists_[index].current_row = -1;
    }
    else if (playlists_[index].current_row >= playlists_[index].paths.size())
    {
        playlists_[index].current_row = static_cast<int>(playlists_[index].paths.size() - 1);
    }

    return true;
}

QString playlist_store::default_playlist_name() { return QStringLiteral("默认列表"); }

int playlist_store::find_index_by_id(const QString &id) const
{
    for (int i = 0; i < playlists_.size(); ++i)
    {
        if (playlists_[i].id == id)
        {
            return i;
        }
    }
    return -1;
}

QString playlist_store::create_playlist_id() const { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
