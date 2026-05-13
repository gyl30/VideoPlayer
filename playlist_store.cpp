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

QList<int> normalize_rows(const QList<int> &rows, int max_size)
{
    QList<int> normalized = rows;
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());

    QList<int> valid_rows;
    for (int row : normalized)
    {
        if (row >= 0 && row < max_size)
        {
            valid_rows.append(row);
        }
    }
    return valid_rows;
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
        entry.collapsed = settings.value("collapsed", false).toBool();
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
        settings.setValue("collapsed", entry.collapsed);
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

bool playlist_store::set_collapsed(const QString &id, bool collapsed)
{
    const int index = find_index_by_id(id);
    if (index < 0)
    {
        return false;
    }

    playlists_[index].collapsed = collapsed;
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

bool playlist_store::copy_rows(const QString &source_id, const QList<int> &rows, const QString &target_id)
{
    if (source_id == target_id)
    {
        return false;
    }

    const int source_index = find_index_by_id(source_id);
    const int target_index = find_index_by_id(target_id);
    if (source_index < 0 || target_index < 0)
    {
        return false;
    }

    const QList<int> valid_rows = normalize_rows(rows, static_cast<int>(playlists_[source_index].paths.size()));
    if (valid_rows.isEmpty())
    {
        return false;
    }

    bool changed = false;
    for (int row : valid_rows)
    {
        const QString &path = playlists_[source_index].paths[row];
        if (!playlists_[target_index].paths.contains(path))
        {
            playlists_[target_index].paths.append(path);
            changed = true;
        }
    }

    if (changed && playlists_[target_index].current_row < 0)
    {
        playlists_[target_index].current_row = 0;
    }

    return changed;
}

bool playlist_store::move_rows(const QString &source_id, const QList<int> &rows, const QString &target_id)
{
    if (source_id == target_id)
    {
        return false;
    }

    const bool copied = copy_rows(source_id, rows, target_id);
    const bool removed = remove_rows(source_id, rows);
    return copied || removed;
}

bool playlist_store::remove_rows(const QString &id, const QList<int> &rows)
{
    const int index = find_index_by_id(id);
    if (index < 0 || rows.isEmpty())
    {
        return false;
    }

    const QList<int> valid_rows = normalize_rows(rows, static_cast<int>(playlists_[index].paths.size()));
    if (valid_rows.isEmpty())
    {
        return false;
    }

    QList<int> sorted_rows = valid_rows;
    std::sort(sorted_rows.begin(), sorted_rows.end(), std::greater<int>());

    bool removed = false;
    const int original_current_row = playlists_[index].current_row;
    int removed_before_current_row = 0;
    bool removed_current_row = false;
    for (int row : sorted_rows)
    {
        if (row == original_current_row)
        {
            removed_current_row = true;
        }
        else if (row < original_current_row)
        {
            ++removed_before_current_row;
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
    else if (original_current_row < 0)
    {
        playlists_[index].current_row = 0;
    }
    else if (removed_current_row)
    {
        const int next_row = original_current_row - removed_before_current_row;
        playlists_[index].current_row = std::clamp(next_row, 0, static_cast<int>(playlists_[index].paths.size() - 1));
    }
    else
    {
        const int next_row = original_current_row - removed_before_current_row;
        playlists_[index].current_row = std::clamp(next_row, 0, static_cast<int>(playlists_[index].paths.size() - 1));
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
