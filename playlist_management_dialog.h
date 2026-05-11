#ifndef PLAYLIST_MANAGEMENT_DIALOG_H
#define PLAYLIST_MANAGEMENT_DIALOG_H

#include <QDialog>
#include "playlist_store.h"

class QListWidget;
class QListWidgetItem;
class QPushButton;

class playlist_management_dialog : public QDialog
{
    Q_OBJECT

   public:
    explicit playlist_management_dialog(const playlist_store &store, QWidget *parent = nullptr);
    ~playlist_management_dialog() override = default;

    [[nodiscard]] const playlist_store &result_store() const;

   private slots:
    void on_source_playlist_changed();
    void on_target_playlist_changed();

   private:
    void setup_ui();
    void setup_connections();
    void populate_playlist_lists();
    void update_song_list(QListWidget *playlist_list, QListWidget *song_list, bool source_side);
    QString current_playlist_id(QListWidget *playlist_list) const;

   private:
    playlist_store temp_store_;

    QListWidget *source_playlists_list_ = nullptr;
    QListWidget *source_songs_list_ = nullptr;
    QListWidget *target_playlists_list_ = nullptr;
    QListWidget *target_songs_list_ = nullptr;

    QPushButton *btn_create_playlist_ = nullptr;
    QPushButton *btn_rename_playlist_ = nullptr;
    QPushButton *btn_copy_ = nullptr;
    QPushButton *btn_move_ = nullptr;
    QPushButton *btn_remove_ = nullptr;
    QPushButton *btn_apply_ = nullptr;
    QPushButton *btn_cancel_ = nullptr;
};

#endif
