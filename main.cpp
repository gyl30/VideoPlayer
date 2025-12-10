#include <QApplication>
#include <QWidget>
#include "log.h"
#include "scoped_exit.h"

int main(int argc, char *argv[])
{
    init_log("player.log");
    DEFER(shutdown_log());

    QApplication app(argc, argv);
    QWidget window;
    window.setWindowTitle("VideoPlayer");
    window.resize(800, 600);
    window.show();

    LOG_INFO("window shown");

    int ret = QApplication::exec();

    LOG_INFO("application exiting with code {}", ret);

    return ret;
}
