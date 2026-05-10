#include <QMouseEvent>
#include <QStyleOptionProgressBar>
#include <QStylePainter>

#include "volumemeter.h"

volume_meter::volume_meter(QWidget *parent) : QProgressBar(parent)
{
    setMouseTracking(true);
    setAutoFillBackground(false);
}

void volume_meter::mousePressEvent(QMouseEvent *event)
{
    setValueFromPosition(event->pos());
}

void volume_meter::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) != 0U)
    {
        setValueFromPosition(event->pos());
    }
}

void volume_meter::paintEvent(QPaintEvent * /*event*/)
{
    QStylePainter painter(this);
    QStyleOptionProgressBar option;
    initStyleOption(&option);

    painter.setPen(QColor("#53B6D4"));
    painter.setBrush(QColor("#0F75A8"));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 2, 2);

    const int num_blocks = 10;
    const int max_value = qMax(1, maximum());
    const int lit_blocks = static_cast<int>((static_cast<double>(value()) / static_cast<double>(max_value)) * num_blocks);

    if (orientation() == Qt::Horizontal)
    {
        const double block_width = static_cast<double>(width()) / static_cast<double>(num_blocks);
        for (int i = 0; i < lit_blocks; ++i)
        {
            const QRectF block_rect(static_cast<double>(i) * block_width, 0.0, block_width, static_cast<double>(height()));
            painter.fillRect(block_rect.adjusted(1.0, 1.0, -1.0, -1.0), bar_color_);
        }
    }
    else
    {
        const double block_height = static_cast<double>(height()) / static_cast<double>(num_blocks);
        for (int i = 0; i < lit_blocks; ++i)
        {
            const double y = static_cast<double>(height()) - (static_cast<double>(i + 1) * block_height);
            const QRectF block_rect(0.0, y, static_cast<double>(width()), block_height);
            painter.fillRect(block_rect.adjusted(1.0, 1.0, -1.0, -1.0), bar_color_);
        }
    }
}

void volume_meter::wheelEvent(QWheelEvent *event)
{
    const int step = 5;
    int new_value = value();

    if (event->angleDelta().y() > 0)
    {
        new_value += step;
    }
    else if (event->angleDelta().y() < 0)
    {
        new_value -= step;
    }

    new_value = qBound(minimum(), new_value, maximum());
    if (new_value != value())
    {
        setValue(new_value);
        emit value_changed(new_value);
    }

    event->accept();
}

void volume_meter::setValueFromPosition(const QPoint &pos)
{
    double ratio = 0.0;
    if (orientation() == Qt::Horizontal)
    {
        ratio = static_cast<double>(pos.x()) / static_cast<double>(qMax(1, width()));
    }
    else
    {
        ratio = static_cast<double>(height() - pos.y()) / static_cast<double>(qMax(1, height()));
    }

    int new_value = static_cast<int>(ratio * static_cast<double>(maximum()));
    new_value = qBound(minimum(), new_value, maximum());
    if (new_value != value())
    {
        setValue(new_value);
        emit value_changed(new_value);
    }
}
