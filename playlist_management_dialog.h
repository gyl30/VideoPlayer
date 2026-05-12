#ifndef PLAYLIST_MANAGEMENT_DIALOG_H
#define PLAYLIST_MANAGEMENT_DIALOG_H

#include <QDialog>
#include <QHash>
#include <QPoint>
#include "playlist_store.h"

class QEvent;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QString;
class QTimer;
class QWidget;

class playlist_management_dialog : public QDialog
{
    Q_OBJECT

   public:
    explicit playlist_management_dialog(const playlist_store &store, QWidget *parent = nullptr);
    ~playlist_management_dialog() override = default;

    [[nodiscard]] const playlist_store &result_store() const;

   protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

   private slots:
    void on_source_playlist_changed();
    void on_target_playlist_changed();
    void on_source_selection_changed();
    void on_create_playlist();
    void on_rename_playlist();
    void on_copy_rows();
    void on_move_rows();
    void on_remove_rows();

   private:
    void setup_ui();
    void setup_connections();
    void populate_playlist_lists();
    void update_song_list(QListWidget *playlist_list, QListWidget *song_list, bool source_side);
    QString current_playlist_id(QListWidget *playlist_list) const;
    QList<int> selected_source_rows() const;
    void update_action_buttons();
    void install_auto_hide_scrollbar(QListWidget *list);
    void set_list_scrollbar_visible(QListWidget *list, bool visible);
    void schedule_hide_list_scrollbar(QListWidget *list);
    QListWidget *list_for_scrollbar_object(const QObject *watched) const;

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
    QWidget *title_bar_ = nullptr;
    QHash<QListWidget *, QTimer *> scrollbar_hide_timers_;

    bool dragging_title_bar_ = false;
    QPoint drag_offset_;
};

#endif
