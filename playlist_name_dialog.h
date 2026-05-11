#ifndef PLAYLIST_NAME_DIALOG_H
#define PLAYLIST_NAME_DIALOG_H

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

class playlist_name_dialog : public QDialog
{
    Q_OBJECT

   public:
    explicit playlist_name_dialog(const QString &title,
                                  const QString &label_text,
                                  const QString &accept_text,
                                  const QString &initial_text = {},
                                  QWidget *parent = nullptr);
    ~playlist_name_dialog() override = default;

    [[nodiscard]] QString text() const;

    static QString get_text(QWidget *parent,
                            const QString &title,
                            const QString &label_text,
                            const QString &accept_text,
                            const QString &initial_text,
                            bool *accepted);

   protected:
    void showEvent(QShowEvent *event) override;

   private:
    void setup_ui(const QString &title, const QString &label_text, const QString &accept_text);
    void center_to_parent();

   private:
    QWidget *title_bar_ = nullptr;
    QLabel *title_label_ = nullptr;
    QLineEdit *line_edit_ = nullptr;
    QPushButton *btn_accept_ = nullptr;
    QPushButton *btn_cancel_ = nullptr;
};

#endif
