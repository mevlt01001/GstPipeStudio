// Capture a repeatable UI walkthrough with real GStreamer helpers and test sources.
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QTabWidget>
#include <QtTest>
#include "canvasscene.h"
#include "canvasview.h"
#include "mainwindow.h"
#include "telemetrypanel.h"
#include "testsupport.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    support::isolateSettings();
    Theme::apply(app);
    auto controller = support::makeController();
    MainWindow window(controller.get());
    window.resize(1280, 760); window.show();
    const QString output = qEnvironmentVariable("GPS_DEMO_DIR", "/tmp/gpspipestudio-demo");
    QDir().mkpath(output);
    int frame = 0;
    auto capture = [&](int count) {
        for (int i = 0; i < count; ++i) {
            QTest::qWait(100);
            window.grab().save(QString("%1/%2.png").arg(output).arg(frame++, 4, 10, QChar('0')));
        }
    };
    capture(12);
    const QString source = support::addSource(*controller, "videotestsrc", support::withProps({{"is-live", "true"}}));
    if (source.isEmpty()) return 2;
    window.scene()->rebuild(); window.view()->fitAll(); capture(12);
    const QString queue = support::addAfter(*controller, source, "src", "queue");
    if (queue.isEmpty()) return 3;
    window.scene()->rebuild(); window.view()->fitAll(); capture(12);
    const QString slow = support::addAfter(*controller, queue, "src", "identity", "sink", support::withProps({{"sleep-time", "80000"}}));
    if (slow.isEmpty()) return 4;
    window.scene()->rebuild(); window.view()->fitAll(); capture(12);
    const QString sink = support::addAfter(*controller, slow, "src", "fakesink", "sink", support::withProps({{"sync", "false"}}));
    if (sink.isEmpty() || !controller->result().isComplete()) return 5;
    window.scene()->rebuild(); window.view()->fitAll(); capture(12);
    if (!controller->arrangePipeline()) return 6;
    window.scene()->rebuild(); window.view()->fitAll(); capture(14);
    auto* panel = window.findChild<TelemetryPanel*>("telemetryPanel");
    if (!panel) return 7;
    window.bottomTabs()->setCurrentWidget(panel);
    if (!controller->startMonitoring()) return 8;
    capture(20);
    panel->findChild<QComboBox*>("monitorElement")->setCurrentText(queue);
    panel->findChild<QComboBox*>("monitorMetric")->setCurrentIndex(1);
    capture(20);
    controller->stopMonitoring();
    support::waitFor([&] { return !controller->monitoring(); }, 4000);
    return frame == 114 ? 0 : 9;
}
