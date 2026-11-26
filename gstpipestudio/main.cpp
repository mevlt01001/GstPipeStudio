// main.cpp – gstpipestudio [file.conf]
#include <QApplication>
#include <QCommandLineParser>

#include "appservices.h"
#include "controller.h"
#include "mainwindow.h"
#include "theme.h"
#include "buildinfo.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(BuildInfo::settingsName());
    app.setApplicationVersion(BuildInfo::version() + " · " + BuildInfo::channel() + " · " + BuildInfo::revision());
    app.setApplicationDisplayName(BuildInfo::title());
    app.setOrganizationName("GstPipeStudio");
    QCommandLineParser parser;
    parser.setApplicationDescription("Visual GStreamer pipeline editor");
    parser.addHelpOption(); parser.addVersionOption(); parser.addPositionalArgument("pipeline", "Pipeline .conf file", "[pipeline]");
    parser.process(app);
    Theme::apply(app);

    AppServices services;  // shared by every tab: the GStreamer check, the catalog, the template library
    Controller controller(services);
    MainWindow window(&controller);  // checks GStreamer first; the catalog loads when it is fine
    window.show();
    window.restoreSession();  // the tabs open when it last closed, then the file asked for
    if (!parser.positionalArguments().isEmpty()) window.openFile(parser.positionalArguments().first());
    return app.exec();
}
