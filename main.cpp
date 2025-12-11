#include <QApplication>
#include "log.h"
#include "mainwindow.h"
#include "scoped_exit.h"

int main(int argc, char *argv[])
{
    init_log("player.log");
    DEFER(shutdown_log());

    QApplication app(argc, argv);

    main_window w;
    w.show();

    return QApplication::exec();
}
