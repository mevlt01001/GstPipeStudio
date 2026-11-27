// main.cpp – gstpipestudio [file.conf]
#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <atomic>
#include <cstdio>
#include <exception>
#include <functional>

#include "buildinfo.h"
#include "gstpipestudio.h"
#include "theme.h"
#include "settings.h"

namespace {

using ErrorSink = std::function<void(const QString& title, const QString& detail)>;

std::atomic<bool> gInMessageHandler{false};
QPointer<QApplication> gApp;
ErrorSink gErrorSink;
QtMessageHandler gPreviousHandler = nullptr;

class StudioApplication final : public QApplication {
public:
    using QApplication::QApplication;

    bool notify(QObject* receiver, QEvent* event) override {
        try {
            return QApplication::notify(receiver, event);
        } catch (const std::exception& e) {
            dispatch("Unexpected error", QString::fromUtf8(e.what()));
        } catch (...) {
            dispatch("Unexpected error", "An unknown internal exception occurred.");
        }
        return false;
    }

private:
    static void dispatch(const QString& title, const QString& detail) {
        std::fprintf(stderr, "[gstpipestudio] %s: %s\n", title.toUtf8().constData(), detail.toUtf8().constData());
        if (gErrorSink) {
            QMetaObject::invokeMethod(qApp, [title, detail] {
                if (gErrorSink) gErrorSink(title, detail);
            }, Qt::QueuedConnection);
        }
    }
};

void studioMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    if (gPreviousHandler) gPreviousHandler(type, context, msg);
    else std::fprintf(stderr, "%s\n", msg.toUtf8().constData());

    if (type == QtFatalMsg) std::fflush(stderr);
    if (type != QtCriticalMsg || gInMessageHandler.exchange(true)) return;
    if (gApp && gErrorSink) {
        QMetaObject::invokeMethod(gApp.data(), [msg] {
            if (gErrorSink) gErrorSink("Internal error", msg);
            gInMessageHandler.store(false);
        }, Qt::QueuedConnection);
    } else {
        gInMessageHandler.store(false);
    }
}

}  // namespace

int main(int argc, char** argv) {
    StudioApplication app(argc, argv);
    gApp = &app;
    gPreviousHandler = qInstallMessageHandler(studioMessageHandler);

    app.setApplicationName(BuildInfo::settingsName());
    app.setApplicationVersion(BuildInfo::version() + " · " + BuildInfo::channel() + " · " + BuildInfo::revision());
    app.setApplicationDisplayName(BuildInfo::title());
    app.setOrganizationName("GstPipeStudio");
    QCommandLineParser parser;
    parser.setApplicationDescription("Visual GStreamer pipeline editor");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("pipeline", "Pipeline .conf file", "[pipeline]");
    parser.process(app);
    Theme::apply(app, Settings::lightTheme() ? Theme::Mode::Light : Theme::Mode::Dark);

    try {
        GstPipeStudio studio;
        gErrorSink = [&studio](const QString& title, const QString& detail) { studio.reportError(title, detail); };
        const int code = studio.run(app, parser.positionalArguments().value(0));
        gErrorSink = {};
        return code;
    } catch (const std::exception& e) {
        gErrorSink = {};
        QMessageBox::critical(nullptr, "GstPipeStudio", QString("Fatal startup error:\n%1").arg(QString::fromUtf8(e.what())));
        return 1;
    } catch (...) {
        gErrorSink = {};
        QMessageBox::critical(nullptr, "GstPipeStudio", "Fatal startup error: an unknown exception occurred.");
        return 1;
    }
}
