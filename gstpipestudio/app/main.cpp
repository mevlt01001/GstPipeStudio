// main.cpp – gstpipestudio [dosya.conf]
#include <QApplication>

#include "controller.h"
#include "mainwindow.h"
#include "theme.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("gstpipestudio");
    Theme::apply(app);

    Controller controller;
    MainWindow window(&controller);
    controller.loadCatalog();
    window.show();
    if (argc > 1) window.openFile(QString::fromLocal8Bit(argv[1]));
    return app.exec();
}
