#ifndef PLAYLIST_STORE_H
#define PLAYLIST_STORE_H

#include <QList>
#include <QSettings>
#include <QString>
#include <QStringList>

struct playlist_entry
{
    QString id;
    QString name;
    QStringList paths;
    int current_row = -1;
    bool collapsed = false;
};

class playlist_store
{
   public:
    playlist_store() = default;

   public:
    void load(QSettings &settings);
    void save(QSettings &settings) const;

    void ensure_default_playlist();

    [[nodiscard]] const QList<playlist_entry> &playlists() const;
    [[nodiscard]] int playlist_count() const;
    [[nodiscard]] QString active_playlist_id() const;
    [[nodiscard]] int active_index() const;
    [[nodiscard]] const playlist_entry *active_playlist() const;
    [[nodiscard]] const playlist_entry *playlist_by_id(const QString &id) const;
    [[nodiscard]] int current_row(const QString &id) const;
    [[nodiscard]] int index_of_path(const QString &id, const QString &path) const;

    QString create_playlist(const QString &name);
    bool rename_playlist(const QString &id, const QString &name);
    bool remove_playlist(const QString &id);
    bool set_active_playlist(const QString &id);
    bool set_current_row(const QString &id, int row);
    bool set_collapsed(const QString &id, bool collapsed);
    bool add_path(const QString &id, const QString &path);
    bool copy_rows(const QString &source_id, const QList<int> &rows, const QString &target_id);
    bool move_rows(const QString &source_id, const QList<int> &rows, const QString &target_id);
    bool remove_rows(const QString &id, const QList<int> &rows);

    [[nodiscard]] static QString default_playlist_name();

   private:
    [[nodiscard]] int find_index_by_id(const QString &id) const;
    [[nodiscard]] QString create_playlist_id() const;

   private:
    QList<playlist_entry> playlists_;
    QString active_playlist_id_;
};

#endif
