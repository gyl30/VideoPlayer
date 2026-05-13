#include "style_loader.h"

#include <QFile>

QString load_stylesheet_resource(const char *resource_path)
{
    QFile file(QString::fromUtf8(resource_path));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return {};
    }

    return QString::fromUtf8(file.readAll());
}
