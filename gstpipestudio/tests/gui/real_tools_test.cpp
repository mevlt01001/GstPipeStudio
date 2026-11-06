// real_tools_test.cpp – the Controller against the real gps-inspect / gps-update / gps-run.
// CMake sets GPS_REAL_BIN_DIR to the folder of the helpers built in the same tree, so it runs with them. It skips
// itself (and says so) only when the build has no helpers and the variable is not set by hand, e.g.
//   GPS_REAL_BIN_DIR=/workspace/build/bin GPS_BUILD_DIR=build-gui docker/studio/test.sh gui
// The GUI depends only on the JSON of SPEC 7, so this is the check that the real tools speak it.
#include <QDir>
#include <QAction>
#include <QTemporaryDir>

#include "canvasscene.h"
#include "canvasview.h"
#include "mainwindow.h"
#include "drawer.h"
#include "pickerpage.h"
#include "theme.h"

#include "testsupport.h"

using namespace support;

class RealToolsTest : public QObject {
    Q_OBJECT

    std::unique_ptr<Controller> c;

private slots:
    void initTestCase() {
        isolateSettings();
        Theme::apply(*qApp);
        QByteArray dir = qgetenv("GPS_REAL_BIN_DIR");
        if (dir.isEmpty()) QSKIP("GPS_REAL_BIN_DIR is not set");
        qputenv("GPS_BIN_DIR", dir);
        qInfo() << "REAL TOOLS in" << dir;
        c = makeController();
    }
    void cleanupTestCase() { c.reset(); }

    void theRealVersionAnswerMeetsTheMinimum() {
        StartupCheck check;
        QSignalSpy done(&check, &StartupCheck::finished);
        check.start();
        QVERIFY(waitFor([&] { return done.count() == 1; }));
        QCOMPARE(check.result().state, StartupResult::Ready);
        qInfo() << "gps-inspect version:" << check.result().statusText();
        QVERIFY(check.result().version.startsWith("1."));
    }

    void catalogHasTheUsualElementsAndNvidiaBadges() {
        QVERIFY(c->catalogLoaded());
        QVERIFY2(c->catalog().size() > 50, qPrintable(QString::number(c->catalog().size())));
        QMap<QString, ListEntry> by;
        for (const ListEntry& e : c->catalog()) by[e.factory] = e;
        for (const char* f : {"videotestsrc", "videoconvert", "fakesink", "queue", "tee", "compositor"}) QVERIFY2(by.contains(f), f);
        QVERIFY(by["videotestsrc"].isSource);
        QVERIFY(!by["videoconvert"].isSource);
        QVERIFY(by["tee"].hasRequestSrc);
        int nvidia = 0;
        for (const ListEntry& e : c->catalog()) nvidia += e.nvidia;
        qInfo() << "factories:" << c->catalog().size() << "nvidia:" << nvidia;
    }

    void elementInfoParses() {
        const ElementInfo* info = need(*c, "videotestsrc");
        QVERIFY(info);
        QVERIFY(info->isSource);
        const PadTemplateInfo* src = info->padTemplate("src", "src");
        QVERIFY(src);
        QVERIFY(!src->structures.isEmpty());
        QVERIFY(!src->structures[0].fields.isEmpty());
        QStringList kinds;
        for (const PropertyInfo& p : info->properties) kinds << p.kind;
        QVERIFY(kinds.contains("enum"));
        QVERIFY(kinds.contains("boolean"));
        const PropertyInfo* pattern = nullptr;
        for (const PropertyInfo& p : info->properties) {
            if (p.name == "pattern") pattern = &p;
        }
        QVERIFY(pattern);
        QVERIFY(pattern->enumValues.size() > 3);
        QVERIFY(need(*c, "compositor")->padTemplate("sink_%u", "sink"));
        QVERIFY(!need(*c, "compositor")->padTemplate("sink_%u", "sink")->padProperties.isEmpty());
        QVERIFY(need(*c, "tee")->padTemplate("src_%u", "src")->isRequest());
    }

    void updateAndRunOnARealPipeline() {
        QString src = addSource(*c, "videotestsrc", withProps({{"num-buffers", "5"}}));
        QVERIFY(!src.isEmpty());
        QCOMPARE(c->result().status, QString("ok"));
        const PadReport* pad = c->result().pad(src, "src", true);
        QVERIFY(pad);
        QVERIFY(!pad->allowedStructures.isEmpty());
        QVERIFY(c->view(src).srcs[0].media.startsWith("video/x-raw"));
        QString convert = addAfter(*c, src, "src", "videoconvert");
        QString sink = addAfter(*c, convert, "src", "fakesink");
        QVERIFY2(c->result().isComplete(), qPrintable(c->result().message));
        QVERIFY(c->setCaps(src, "src", "video/x-raw, width=(int)320", {}));
        QVERIFY(settle(*c));
        QVERIFY2(c->result().ok(), qPrintable(c->result().message));
        QVERIFY(c->result().pad(src, "src", true)->caps.contains("width=(int)320"));

        QTemporaryDir dir;
        QString error;
        QVERIFY2(c->save(dir.filePath("real.conf"), &error), qPrintable(error));
        QSignalSpy ended(c.get(), &Controller::runFinished);
        QVERIFY(c->run());
        QVERIFY(waitFor([&] { return ended.count() == 1; }, 30000));
        QCOMPARE(ended[0][0].toString(), QString("eos"));
    }

    void importsARealLaunchPipelineIntoAnUnsavedCanvas() {
        c->newPlan();
        MainWindow window(c.get());
        window.show();
        QVERIFY(window.findChild<QAction*>("actionImportLaunch"));
        QVERIFY(c->importLaunch("videotestsrc num-buffers=5 is-live=true ! queue ! fakesink sync=false"));
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 3 && !c->busy(); }, 15000));
        QVERIFY2(c->result().isComplete(), qPrintable(c->result().message));
        QCOMPARE(c->plan().links.size(), size_t(2));
        QVERIFY(c->filePath().isEmpty());
        QVERIFY(c->dirty());
        window.scene()->rebuild();
        QVERIFY(window.scene()->items().size() >= 3);
        window.view()->fitAll();
        QDir().mkpath(shotDir());
        QVERIFY(window.grab().save(QString("%1/imported_launch_pipeline.png").arg(shotDir())));
        QSignalSpy finished(c.get(), &Controller::runFinished);
        QVERIFY(c->run());
        QVERIFY(waitFor([&] { return finished.count() == 1; }, 10000));
        QCOMPARE(finished[0][0].toString(), QString("eos"));
    }

    // The same screenshots as with the fakes, but with the real catalog (hundreds of factories) and real caps.
    void screenshotsWithTheRealCatalog() {
        c->newPlan();
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc", withProps({{"pattern", "ball"}}));
        QString convert = addAfter(*c, src, "src", "videoconvert");
        QString nv = addAfter(*c, convert, "src", "nvvideoconvert");
        QTest::qWait(200);
        w.scene()->rebuild();
        w.view()->fitAll();
        QTest::qWait(200);
        QDir().mkpath(shotDir());
        QVERIFY(w.grab().save(QString("%1/ux_30_real_helpers.png").arg(shotDir())));

        QStringList compatible;
        c->fetchCompatible(c->padAllowed(nv, "src"), [&](const QStringList& l) { compatible = l; });
        QVERIFY(waitFor([&] { return !compatible.isEmpty(); }));
        PickerData data;
        data.compatible = compatible;
        for (const ListEntry& e : c->catalog()) {
            if (!e.isSource) data.candidates << e;
        }
        QElapsedTimer timer;
        timer.start();
        w.drawer()->present(new PickerPage(data), "Add after " + nv + " · src", "video/x-raw", Drawer::Replace);
        qInfo() << "picker with" << data.candidates.size() << "candidates opened in" << timer.elapsed() << "ms," << compatible.size() << "compatible";
        QVERIFY(timer.elapsed() < 2000);
        QVERIFY(waitFor([&] { return w.drawer()->width() == Drawer::kWidth; }, 3000));
        QTest::qWait(150);
        QVERIFY(w.grab().save(QString("%1/ux_31_real_drawer_picker.png").arg(shotDir())));
    }

    void aBadPropertyShowsAsAnErrorOnTheElement() {
        c->newPlan();
        QString src = addSource(*c, "filesrc", withProps({{"location", "/does/not/exist.avi"}}));
        QVERIFY(!src.isEmpty());
        qInfo() << "filesrc without file:" << c->result().status << c->result().message;
        QVERIFY(!c->result().ok() || !c->result().isComplete());
    }
};

QTEST_MAIN(RealToolsTest)
#include "real_tools_test.moc"
