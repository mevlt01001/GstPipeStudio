// main.cpp – gstpipestudio [file.conf]
#include <QApplication>
#include <QCommandLineParser>

#include "buildinfo.h"
#include "gstpipestudio.h"
#include "theme.h"

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

    GstPipeStudio studio;
    return studio.run(app, parser.positionalArguments().value(0));
}
