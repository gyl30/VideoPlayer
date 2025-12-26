#include <QApplication>
#include "log.h"
#include "scoped_exit.h"
#include "main_window.h"

Q_DECLARE_METATYPE(std::shared_ptr<media_frame>);

static void start(const std::string& app_name) { LOG_INFO("{} start", app_name); }
static void shutdown(const std::string& app_name) { LOG_INFO("{} shutdown", app_name); }

int main(int argc, char* argv[])
{
    std::string app_name(argv[0]);
    init_log(app_name + ".log");
    DEFER(shutdown_log());
    start(app_name);
    DEFER(shutdown(app_name));
    const QApplication app(argc, argv);
    main_window window;
    window.show();
    return QApplication::exec();
}
