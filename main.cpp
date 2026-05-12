#include <QApplication>
#include <QIcon>
#include "log.h"
#include "scoped_exit.h"
#include "main_window.h"

Q_DECLARE_METATYPE(std::shared_ptr<media_frame>);

static void start(const std::string& app_name) { LOG_INFO("{} start", app_name); }
static void shutdown(const std::string& app_name) { LOG_INFO("{} shutdown", app_name); }

#if defined(QT_NEEDS_QMAIN)
int qMain(int argc, char* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    std::string app_name(argv[0]);
    init_log(app_name + ".log");
    DEFER(shutdown_log());
    start(app_name);
    DEFER(shutdown(app_name));
    const QApplication app(argc, argv);
    QApplication::setApplicationDisplayName("视频播放器");
    QApplication::setWindowIcon(QIcon(":/icons/app_icon.svg"));
    main_window window;
    window.setWindowIcon(QIcon(":/icons/app_icon.svg"));
    window.show();
    return QApplication::exec();
}
