// real_tools_test.cpp – the Controller against the real gps-inspect / gps-update / gps-run.
// CMake sets GPS_REAL_BIN_DIR to the folder of the helpers built in the same tree, so it runs with them. It skips
// itself (and says so) only when the build has no helpers and the variable is not set by hand, e.g.
//   GPS_REAL_BIN_DIR=/workspace/build/bin GPS_BUILD_DIR=build-gui docker/studio/test.sh gui
// The GUI depends only on the JSON of SPEC 7, so this is the check that the real tools speak it.
#include <QDir>
#include <QAction>
#include <QClipboard>
#include <QLabel>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QFile>
#include "pluginscaffolder.h"

#include "canvasscene.h"
#include "canvasview.h"
#include "document.h"
#include "mainwindow.h"
#include "notificationoverlay.h"
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
        QVERIFY(c->services().catalog().loaded());
        QVERIFY2(c->services().catalog().entries().size() > 50, qPrintable(QString::number(c->services().catalog().entries().size())));
        QMap<QString, FactoryEntry> by;
        for (const FactoryEntry& e : c->services().catalog().entries()) by[fromStd(e.factory)] = e;
        for (const char* f : {"videotestsrc", "videoconvert", "fakesink", "queue", "tee", "compositor"}) QVERIFY2(by.contains(f), f);
        QVERIFY(by["videotestsrc"].isSource);
        QVERIFY(!by["videoconvert"].isSource);
        QVERIFY(by["tee"].hasRequestSrc);
        int nvidia = 0;
        for (const FactoryEntry& e : c->services().catalog().entries()) nvidia += e.nvidia;
        qInfo() << "factories:" << c->services().catalog().entries().size() << "nvidia:" << nvidia;
    }

    void elementInfoParses() {
        const ElementInfo* info = need(*c, "videotestsrc");
        QVERIFY(info);
        QVERIFY(info->entry.isSource);
        const PadTemplateInfo* src = info->padTemplate("src", "src");
        QVERIFY(src);
        QVERIFY(!src->structures.empty());
        QVERIFY(!src->structures[0].fields.empty());
        QStringList kinds;
        for (const PropertyInfo& p : info->properties) kinds << fromStd(p.kind);
        QVERIFY(kinds.contains("enum"));
        QVERIFY(kinds.contains("boolean"));
        const PropertyInfo* pattern = nullptr;
        for (const PropertyInfo& p : info->properties) {
            if (p.name == "pattern") pattern = &p;
        }
        QVERIFY(pattern);
        QVERIFY(pattern->enumValues.size() > 3);
        QVERIFY(need(*c, "compositor")->padTemplate("sink_%u", "sink"));
        QVERIFY(!need(*c, "compositor")->padTemplate("sink_%u", "sink")->padProperties.empty());
        QVERIFY(need(*c, "tee")->padTemplate("src_%u", "src")->isRequest());
    }

    void pluginDiagnosticsAndProtectedRegions() {
        const auto diagnostics = DiagnosticParser::parse("/tmp/plugin.cpp:17:9: error: invalid value\n"
                                                         "/tmp/other:4: warning: unused variable\n"
                                                         "undefined reference to foo\n");
        QCOMPARE(diagnostics.size(), 2);
        QCOMPARE(diagnostics.front().file, QString("/tmp/plugin.cpp"));
        QCOMPARE(diagnostics.front().line, 17);
        QCOMPARE(diagnostics.front().column, 9);
        QCOMPARE(diagnostics.back().severity, QString("warning"));

        PluginSpec spec;
        spec.id = "gps_regions";
        spec.longName = "Specification test";
        auto source = PluginScaffolder::source(spec);
        source.replace("(void)frame;", "(void)frame; /* user algorithm */");
        PluginPropertySpec count;
        count.name = "count";
        count.type = PluginPropertyType::UInt64;
        count.defaultValue = "18446744073709551615";
        spec.properties << count;
        QString error;
        QVERIFY(PluginScaffolder::updateSpecRegions(spec, &source, &error));
        QVERIFY(source.contains("/* user algorithm */"));
        QVERIFY(source.contains("18446744073709551615ULL"));
        PluginSpec roundTrip;
        QVERIFY(PluginSpec::fromJson(spec.toJson(), &roundTrip, &error));
        QCOMPARE(roundTrip.toJson(), spec.toJson());
        source.replace("// END GPS_PLUGIN_PROPERTIES", "// marker deleted");
        const auto broken = source;
        QVERIFY(!PluginScaffolder::updateSpecRegions(spec, &source, &error));
        QCOMPARE(source, broken);
    }

    void buildsReloadsAndRunsGeneratedCustomPlugins() {
        QTemporaryDir plugins;
        QVERIFY(plugins.isValid());
        const auto prior = qgetenv("GPS_USER_DIR");
        qputenv("GPS_USER_DIR", plugins.path().toUtf8());
        const auto restore = qScopeGuard([&] { prior.isNull() ? qunsetenv("GPS_USER_DIR") : qputenv("GPS_USER_DIR", prior); });
        AppServices local;
        Controller opened(local);
        auto& library = local.plugins();
        auto& catalog = local.catalog();
        catalog.load();
        QTRY_VERIFY_WITH_TIMEOUT(catalog.loaded(), 30000);

        QString error;
        for (const auto type : {PluginArchetype::VideoFilter, PluginArchetype::AudioFilter, PluginArchetype::Transform,
                                PluginArchetype::Source, PluginArchetype::Sink, PluginArchetype::GeneralElement}) {
            PluginSpec spec;
            spec.id = "gps_starter_" + QString::number(static_cast<int>(type));
            spec.longName = "Starter " + spec.id;
            spec.archetype = type;
            if (type == PluginArchetype::AudioFilter) {
                for (auto& port : spec.ports) {
                    port.capsPreset = PluginCaps::AudioRaw;
                    port.capsDetail = "S16LE";
                }
            }
            if (type == PluginArchetype::Source) {
                spec.ports.removeFirst();
                spec.uriProtocol = "gps-demo";
            }
            if (type == PluginArchetype::Sink) spec.ports.removeLast();
            if (type == PluginArchetype::GeneralElement) {
                spec.ports.front().inputMode = PluginInput::BoundedQueue;
                PluginPortSpec param{"alpha", PluginDirection::Sink, PluginPresence::Always, PluginCaps::CustomMime, "application/x-float"};
                param.inputMode = PluginInput::Mailbox;
                param.mailboxDefault = "0.5";
                spec.ports << param;
            }
            QVERIFY2(library.createPlugin(spec, &error), qPrintable(error));
            QCOMPARE(library.state(spec.id), PluginBuildState::Untested);
            std::optional<PluginBuildResult> result;
            library.buildPlugin(spec.id, this, [&](PluginBuildResult built) { result = std::move(built); });
            QCOMPARE(library.state(spec.id), PluginBuildState::Building);
            QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 60000);
            QVERIFY2(result->ok, qPrintable(result->error + "\n" + result->output));
            QVERIFY2(result->diagnostics.isEmpty(), qPrintable(result->output));
            QCOMPARE(library.state(spec.id), PluginBuildState::Built);
            QVERIFY(catalog.info(spec.id));
        }

        const QString id = "gps_starter_6";
        QVERIFY(opened.importLaunch("videotestsrc num-buffers=5 ! " + id + " ! fakesink sync=false"));
        QTRY_VERIFY_WITH_TIMEOUT(opened.plan().elements.size() == 3 && !opened.busy(), 20000);
        QVERIFY2(opened.result().ok(), opened.result().message.c_str());
        QVERIFY(opened.result().complete());
        QSignalSpy ended(&opened, &Controller::runFinished);
        QVERIFY(opened.run());
        QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 20000);
        QCOMPARE(ended.front().front().toString(), QString("eos"));

        auto spec = library.spec(id).value();
        PluginPropertySpec added;
        added.name = "gain";
        added.defaultValue = "25";
        spec.properties << added;
        const auto* stable = catalog.info(id);
        QVERIFY2(library.updateSpec(id, spec, &error), qPrintable(error));
        QCOMPARE(library.state(id), PluginBuildState::Outdated);
        std::optional<PluginBuildResult> result;
        library.buildPlugin(id, this, [&](PluginBuildResult built) { result = std::move(built); });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 60000);
        QVERIFY2(result->ok, qPrintable(result->error + "\n" + result->output));
        QCOMPARE(catalog.info(id), stable);
        QVERIFY(std::any_of(stable->properties.begin(), stable->properties.end(), [](const auto& p) { return p.name == "gain"; }));
        QVERIFY(settle(opened));

        QFile source(library.sourcePath(id));
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QString good = QString::fromUtf8(source.readAll());
        source.close();
        QVERIFY2(library.saveSource(id, good + "\n#error intentional test failure\n", &error), qPrintable(error));
        result.reset();
        library.buildPlugin(id, this, [&](PluginBuildResult built) { result = std::move(built); });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 60000);
        QVERIFY(!result->ok);
        QVERIFY(!result->diagnostics.isEmpty());
        QCOMPARE(library.state(id), PluginBuildState::Failed);
        QVERIFY(library.placementError(id).isEmpty());  // the last verified module stays available
    }

    void updateAndRunOnARealPipeline() {
        QString src = addSource(*c, "videotestsrc", withProps({{"num-buffers", "5"}}));
        QVERIFY(!src.isEmpty());
        QCOMPARE(fromStd(c->result().status), QString("ok"));
        const PadReport* pad = c->result().pad(toStd(src), toStd("src"), true);
        QVERIFY(pad);
        QVERIFY(!pad->allowedStructures.empty());
        QVERIFY(c->view(src).srcs[0].media.startsWith("video/x-raw"));
        QString convert = addAfter(*c, src, "src", "videoconvert");
        QString sink = addAfter(*c, convert, "src", "fakesink");
        QVERIFY2(c->result().complete(), c->result().message.c_str());
        QVERIFY(c->setCaps(src, "src", "video/x-raw, width=(int)320", {}));
        QVERIFY(settle(*c));
        QVERIFY2(c->result().ok(), c->result().message.c_str());
        QVERIFY(fromStd(c->result().pad(toStd(src), toStd("src"), true)->caps).contains("width=(int)320"));

        QTemporaryDir dir;
        QVERIFY(saved(saveAs(*c, dir.filePath("real.conf"))));
        QSignalSpy ended(c.get(), &Controller::runFinished);
        QVERIFY(c->run());
        QVERIFY(waitFor([&] { return ended.count() == 1; }, 30000));
        QCOMPARE(ended[0][0].toString(), QString("eos"));
    }

    void importsARealLaunchPipelineIntoAnUnsavedCanvas() {
        c->newPlan(std::make_unique<UntitledTarget>(1));
        MainWindow window(c.get());
        window.show();
        QVERIFY(window.findChild<QAction*>("actionImportLaunch"));
        QVERIFY(c->importLaunch("videotestsrc num-buffers=5 is-live=true ! queue ! fakesink sync=false"));
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 3 && !c->busy(); }, 15000));
        QVERIFY2(c->result().complete(), c->result().message.c_str());
        QCOMPARE(c->plan().links.size(), size_t(2));
        QVERIFY(c->target().path().isEmpty());
        QVERIFY(c->modified());
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

    void addsLaunchPipelineIntoActiveProjectAndClipboardPaste() {
        QTemporaryDir dir;
        const QString projectPath = dir.filePath("proj/pipeline.conf");
        c->newPlan(std::make_unique<ProjectConfTarget>(projectPath));
        MainWindow window(c.get());
        window.show();

        QVERIFY(window.front()->controller()->addLaunch("videotestsrc num-buffers=5 is-live=true ! queue ! fakesink sync=false"));
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 3 && !c->busy() && !c->canvasLayout().busy(); }, 15000));
        QCOMPARE(window.documents().size(), 1);
        QCOMPARE(c->target().path(), projectPath);
        const auto& els = c->plan().elements;
        QVERIFY(els[0].x != els[1].x || els[1].x != els[2].x);

        QGuiApplication::clipboard()->setText("gst-launch-1.0 audiotestsrc num-buffers=5 ! fakesink sync=false");
        window.front()->paste();
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 5 && !c->busy() && !c->canvasLayout().busy(); }, 15000));
        QCOMPARE(window.documents().size(), 1);

        window.notifications()->dismissAll();
        QVERIFY(c->addLaunch("not_a_real_element ! fakesink"));
        QVERIFY(waitFor([&] { return window.notifications()->count() >= 1 && !c->busy(); }, 15000));
        QCOMPARE(window.notifications()->findChild<QLabel*>("toastTitle")->text(), QString("Import failed"));
    }

    // The same screenshots as with the fakes, but with the real catalog (hundreds of factories) and real caps.
    void screenshotsWithTheRealCatalog() {
        c->newPlan(std::make_unique<UntitledTarget>(1));
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
        c->services().catalog().fetchCompatible(c->padAllowed(nv, "src"), c.get(), [&](const QStringList& l) { compatible = l; });
        QVERIFY(waitFor([&] { return !compatible.isEmpty(); }));
        PickerData data;
        data.compatible = compatible;
        for (const FactoryEntry& e : c->services().catalog().entries()) {
            if (!e.isSource) data.candidates.push_back(e);
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
        c->newPlan(std::make_unique<UntitledTarget>(1));
        QString src = addSource(*c, "filesrc", withProps({{"location", "/does/not/exist.avi"}}));
        QVERIFY(!src.isEmpty());
        qInfo() << "filesrc without file:" << c->result().status << c->result().message;
        QVERIFY(!c->result().ok() || !c->result().complete());
    }
};

QTEST_MAIN(RealToolsTest)
#include "real_tools_test.moc"
