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

playlist_name_dialog::playlist_name_dialog(const QString &title,
                                           const QString &label_text,
                                           const QString &accept_text,
                                           const QString &initial_text,
                                           QWidget *parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setModal(true);
    resize(420, 176);
    setStyleSheet(
        "QDialog {"
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
        "QLabel#dialogLabel {"
        "    color: #eef4fa;"
        "    font-size: 14px;"
        "}"
        "QLineEdit {"
        "    background: #0b1929;"
        "    color: #f5fbff;"
        "    border: 1px solid #1e7dbd;"
        "    border-radius: 4px;"
        "    padding: 8px 10px;"
        "    min-height: 22px;"
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
        "QPushButton#dialogActionButton {"
        "    background: transparent;"
        "    color: #f5fbff;"
        "    border: none;"
        "    border-radius: 2px;"
        "    min-width: 68px;"
        "    min-height: 38px;"
        "    padding: 0 12px;"
        "}"
        "QPushButton#dialogActionButton:hover {"
        "    background: rgba(255, 255, 255, 0.12);"
        "    color: #ffffff;"
        "}"
        "QPushButton#dialogActionButton:pressed {"
        "    background: rgba(8, 29, 49, 0.9);"
        "}"
    );

    setup_ui(title, label_text, accept_text);
    line_edit_->setText(initial_text);
    line_edit_->selectAll();
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
    title_bar_->setFixedHeight(42);

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
    body_layout->setContentsMargins(16, 16, 16, 16);
    body_layout->setSpacing(12);

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
    btn_cancel_->setCursor(Qt::PointingHandCursor);

    btn_accept_ = new QPushButton(accept_text, button_row);
    btn_accept_->setObjectName("dialogActionButton");
    btn_accept_->setCursor(Qt::PointingHandCursor);
    btn_accept_->setDefault(true);

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
    connect(line_edit_, &QLineEdit::returnPressed, this, &QDialog::accept);
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
