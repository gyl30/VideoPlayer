#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QStandardPaths>
#include "log.h"
#include "scoped_exit.h"
#include "main_window.h"

Q_DECLARE_METATYPE(std::shared_ptr<media_frame>);

static void start(const std::string& app_name) { LOG_INFO("{} start", app_name); }
static void shutdown(const std::string& app_name) { LOG_INFO("{} shutdown", app_name); }

static QString default_log_path()
{
    QString log_dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (log_dir.isEmpty())
    {
        log_dir = QDir::currentPath();
    }

    QDir().mkpath(log_dir);
    return QDir(log_dir).filePath(QStringLiteral("VideoPlayer.log"));
}

#if defined(QT_NEEDS_QMAIN)
int qMain(int argc, char* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    std::string app_name(argv[0]);
    const QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("gyl30"));
    QCoreApplication::setApplicationName(QStringLiteral("VideoPlayer"));
    QApplication::setApplicationDisplayName("视频播放器");
    QApplication::setWindowIcon(QIcon(":/icons/app_icon.svg"));
    init_log(default_log_path().toStdString());
    DEFER(shutdown_log());
    start(app_name);
    DEFER(shutdown(app_name));
    main_window window;
    window.setWindowIcon(QIcon(":/icons/app_icon.svg"));
    window.show();
    return QApplication::exec();
}
