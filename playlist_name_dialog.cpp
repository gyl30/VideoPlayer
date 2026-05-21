#include "playlist_name_dialog.h"

#include <QHBoxLayout>
#include <QGuiApplication>
#include <QInputMethod>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include "style_loader.h"

playlist_name_dialog::playlist_name_dialog(const QString &title,
                                           const QString &label_text,
                                           const QString &accept_text,
                                           const QString &initial_text,
                                           QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    resize(380, 168);
    setObjectName("playlistNameDialog");
    setStyleSheet(load_stylesheet_resource(":/styles/playlist_name_dialog.qss"));

    setup_ui(title, label_text, accept_text);
    line_edit_->setText(initial_text);
    line_edit_->selectAll();
    update_accept_button_state();
}

QString playlist_name_dialog::text() const
{
    return line_edit_ == nullptr ? QString() : line_edit_->text();
}

QString playlist_name_dialog::get_text(QWidget *parent,
                                       const QString &title,
                                       const QString &label_text,
                                       const QString &accept_text,
                                       const QString &initial_text,
                                       bool *accepted)
{
    playlist_name_dialog dialog(title, label_text, accept_text, initial_text, parent);
    const bool result = dialog.exec() == QDialog::Accepted;
    if (accepted != nullptr)
    {
        *accepted = result;
    }
    return result ? dialog.text() : QString();
}

void playlist_name_dialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    center_to_parent();
    QTimer::singleShot(
        0,
        this,
        [this]()
        {
            if (line_edit_ == nullptr)
            {
                return;
            }

            activateWindow();
            raise();
            line_edit_->setFocus(Qt::OtherFocusReason);
            line_edit_->selectAll();
            QGuiApplication::inputMethod()->reset();
        });
}

void playlist_name_dialog::setup_ui(const QString &title, const QString &label_text, const QString &accept_text)
{
    auto *main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);

    title_bar_ = new QWidget(this);
    title_bar_->setObjectName("dialogTitleBar");
    title_bar_->setFixedHeight(44);

    auto *title_layout = new QHBoxLayout(title_bar_);
    title_layout->setContentsMargins(14, 0, 0, 0);
    title_layout->setSpacing(0);

    title_label_ = new QLabel(title, title_bar_);
    title_label_->setObjectName("dialogTitleLabel");

    auto *btn_close = new QPushButton(QIcon(":/icons/title-close.svg"), QString(), title_bar_);
    btn_close->setObjectName("dialogCloseButton");
    btn_close->setCursor(Qt::PointingHandCursor);
    btn_close->setIconSize(QSize(14, 14));
    btn_close->setToolTip("关闭");

    title_layout->addWidget(title_label_);
    title_layout->addStretch(1);
    title_layout->addWidget(btn_close);

    auto *body = new QWidget(this);
    auto *body_layout = new QVBoxLayout(body);
    body_layout->setContentsMargins(16, 14, 16, 14);
    body_layout->setSpacing(10);

    auto *label = new QLabel(label_text, body);
    label->setObjectName("dialogLabel");

    line_edit_ = new QLineEdit(body);
    line_edit_->setAttribute(Qt::WA_InputMethodEnabled, true);

    auto *button_row = new QWidget(body);
    auto *button_layout = new QHBoxLayout(button_row);
    button_layout->setContentsMargins(0, 0, 0, 0);
    button_layout->setSpacing(8);

    btn_cancel_ = new QPushButton("取消", button_row);
    btn_cancel_->setObjectName("dialogActionButton");
    btn_cancel_->setProperty("role", "secondary");
    btn_cancel_->setCursor(Qt::PointingHandCursor);

    btn_accept_ = new QPushButton(accept_text, button_row);
    btn_accept_->setObjectName("dialogActionButton");
    btn_accept_->setProperty("role", "primary");
    btn_accept_->setCursor(Qt::PointingHandCursor);
    btn_accept_->setDefault(false);
    btn_accept_->setAutoDefault(false);

    button_layout->addStretch(1);
    button_layout->addWidget(btn_cancel_);
    button_layout->addWidget(btn_accept_);

    body_layout->addWidget(label);
    body_layout->addWidget(line_edit_);
    body_layout->addStretch(1);
    body_layout->addWidget(button_row);

    main_layout->addWidget(title_bar_);
    main_layout->addWidget(body);

    connect(btn_close, &QPushButton::clicked, this, &QDialog::reject);
    connect(btn_cancel_, &QPushButton::clicked, this, &QDialog::reject);
    connect(btn_accept_, &QPushButton::clicked, this, &QDialog::accept);
    connect(line_edit_, &QLineEdit::textChanged, this, [this](const QString &) { update_accept_button_state(); });
}

void playlist_name_dialog::center_to_parent()
{
    QWidget *parent_widget = parentWidget();
    if (parent_widget == nullptr)
    {
        return;
    }

    const QRect parent_rect = parent_widget->frameGeometry();
    move(parent_rect.center() - rect().center());
}

void playlist_name_dialog::update_accept_button_state()
{
    if (line_edit_ == nullptr || btn_accept_ == nullptr)
    {
        return;
    }

    const bool has_text = !line_edit_->text().trimmed().isEmpty();
    btn_accept_->setEnabled(has_text);
    btn_accept_->setDefault(has_text);
}
