// screens_test.cpp – saves screenshots of the main window, the drawer pages and the setup page in key states to GPS_SHOT_DIR.
// They are looked at by a human (or by Claude with the Read tool); the test only checks that they were written
// and are not blank.
#include <QDir>
#include <QMenu>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>

#include "canvasscene.h"
#include "canvasview.h"
#include "capspage.h"
#include "confirmdialog.h"
#include "drawer.h"
#include "elementitem.h"
#include "flows.h"
#include "mainwindow.h"
#include "pickerpage.h"
#include "propertypage.h"
#include "setuppage.h"
#include "testsupport.h"
#include "validationindicator.h"
#include "theme.h"

using namespace support;

class ScreensTest : public QObject {
    Q_OBJECT

    std::unique_ptr<Controller> c;

    void settleWindow(MainWindow& w) {
        QVERIFY(settle(*c));
        QTest::qWait(150);
        w.scene()->rebuild();
        w.view()->fitAll();
        QTest::qWait(150);
    }

    void save(QWidget* widget, const QString& name) {
        QDir().mkpath(shotDir());
        QImage image = widget->grab().toImage();
        QVERIFY2(image.width() > 100 && image.height() > 50, qPrintable(name));
        QSet<QRgb> colours;
        for (int y = 0; y < image.height(); y += 7) {
            for (int x = 0; x < image.width(); x += 7) colours.insert(image.pixel(x, y));
        }
        QVERIFY2(colours.size() > 3, qPrintable(name + " looks blank"));
        QVERIFY(image.save(QString("%1/%2.png").arg(shotDir(), name)));
    }

    // A pipeline with a tee, a compositor, a caps choice, a queue, an NVIDIA element and an exposed parameter.
    void buildRichPipeline() {
        ElementEdit expose;
        expose.props = {{"pattern", "ball"}, {"num-buffers", "300"}};
        expose.exposed = {"num-buffers"};
        QString cam = addSource(*c, "videotestsrc", expose);
        ElementEdit three;
        three.requestSrc["src_%u"] = 2;
        QString tee = addAfter(*c, cam, "src", "tee", "sink", three);
        QString q = addAfter(*c, tee, "src_%u#0", "queue");
        QString mix = addAfter(*c, q, "src", "compositor", "sink_%u", withProps({{"background", "black"}}));
        QString second = addAfter(*c, tee, "src_%u#1", "videoconvert");
        need(*c, "compositor");
        QVERIFY(c->linkExisting(second, "src", mix, "sink_%u"));
        QVERIFY(settle(*c));
        QVERIFY(c->setCaps(second, "src", "video/x-raw, format=(string)NV12, width=(int)640", {{"xpos", "320"}, {"alpha", "0.5"}}));
        QVERIFY(settle(*c));
        QString nv = addAfter(*c, mix, "src", "nvvideoconvert");
        QString text = addAfter(*c, nv, "src", "textoverlay", "video_sink", withProps({{"text", "GstPipeStudio"}}));
        addAfter(*c, text, "src", "fakesink");
    }

private slots:
    void initTestCase() {
        isolateSettings();
        Theme::apply(*qApp);
    }
    void init() { c = makeController(); }
    void cleanup() { c.reset(); }

    void emptyCanvas() {
        MainWindow w(c.get());
        w.show();
        QTest::qWait(200);
        save(&w, "ux_01_empty_canvas");
    }

    void firstSource() {
        MainWindow w(c.get());
        w.show();
        addSource(*c, "videotestsrc", withProps({{"pattern", "snow"}, {"is-live", "true"}}));
        settleWindow(w);
        save(&w, "ux_02_first_source");
    }

    void completePipeline() {
        MainWindow w(c.get());
        w.show();
        buildRichPipeline();
        QVERIFY(c->result().isComplete());
        settleWindow(w);
        save(&w, "ux_03_finished_pipeline");
        w.view()->resetTransform();
        w.view()->centerOn(w.scene()->element("tee_1"));
        QTest::qWait(100);
        save(w.view(), "ux_03b_canvas_1to1");
        w.bottomTabs()->setCurrentIndex(0);
        QTest::qWait(100);
        save(&w, "ux_04_log_panel");
    }

    void demuxWithSometimesPadsAndUnknownData() {
        MainWindow w(c.get());
        w.show();
        QString file = addSource(*c, "filesrc", withProps({{"location", "/data/a.avi"}}));
        QString demux = addAfter(*c, file, "src", "avidemux");
        addAfter(*c, demux, "video_%u#0", "jpegdec");
        addSource(*c, "filesrc", withProps({{"location", "/data/random.bin"}}));
        addSource(*c, "nvarguscamerasrc");
        settleWindow(w);
        save(&w, "ux_05_sometimes_and_unknown");
    }

    void failedUpdate() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString conv = addAfter(*c, src, "src", "videoconvert");
        addAfter(*c, conv, "src", "fakesink", "sink", withProps({{"fake-error", "Could not open resource for writing: /readonly/out.mp4"}}));
        settleWindow(w);
        QVERIFY(!c->canSave());
        save(&w, "ux_06_failed_validation");
    }

    void refusedLink() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        addAfter(*c, src, "src", "jpegdec");
        settleWindow(w);
        save(&w, "ux_07_refused_link");
    }

    void crashedHelper() {
        MainWindow w(c.get());
        w.show();
        addSource(*c, "videotestsrc");
        QString bad = addSource(*c, "audiotestsrc", withProps({{"fake-crash", "1"}}));
        settleWindow(w);
        QCOMPARE(c->result().status, QString("crashed"));
        save(&w, "ux_08_crashed_helper");
    }

    // Waits for the 150 ms slide to end, then saves the whole window.
    void saveWithDrawer(MainWindow& w, const QString& name) {
        QVERIFY(waitFor([&] { return w.drawer()->width() == Drawer::kWidth; }, 3000));
        QTest::qWait(100);
        save(&w, name);
    }

    void drawerPages() {
        MainWindow w(c.get());
        w.show();
        buildRichPipeline();
        settleWindow(w);
        QString free = addSource(*c, "videotestsrc");
        settleWindow(w);

        // Picker: a free pad of a second source, with an existing compositor pad on offer.
        w.flows()->fromFreePad(free, "src");
        auto* picker = w.drawer()->page<PickerPage>();
        QVERIFY(waitFor([&] { return (picker = w.drawer()->page<PickerPage>()) != nullptr; }));
        saveWithDrawer(w, "ux_10_drawer_picker");

        // Properties of the element chosen there.
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "propdemo");
        QTest::keyClick(picker->findChild<QLineEdit*>("searchBox"), Qt::Key_Return);
        auto* props = static_cast<PropertyPage*>(nullptr);
        QVERIFY(waitFor([&] { return (props = w.drawer()->page<PropertyPage>()) != nullptr; }));
        saveWithDrawer(w, "ux_11_drawer_properties");
        w.drawer()->dismiss();

        // Caps and pad settings of the compositor link.
        QVERIFY(waitFor([&] { return !w.drawer()->isOpen(); }));
        w.flows()->editCaps("videoconvert_4", "src");
        QVERIFY(w.drawer()->page<CapsPage>());
        saveWithDrawer(w, "ux_12_drawer_caps");
        w.drawer()->dismiss();
        QTest::qWait(300);

        // Add source.
        w.flows()->chooseSource();
        QVERIFY(w.drawer()->page<PickerPage>());
        saveWithDrawer(w, "ux_13_drawer_add_source");
        w.drawer()->dismiss();
        QTest::qWait(300);

        ConfirmDialog confirm("Remove elements", "These 3 elements will be removed, with 3 links",
                              {"nvvideoconvert_5   (nvvideoconvert)", "textoverlay_6   (textoverlay)", "fakesink_7   (fakesink)"}, "Remove 3 elements");
        confirm.show();
        QTest::qWait(100);
        save(&confirm, "ux_14_confirm");
    }

    void menus() {
        MainWindow w(c.get());
        w.show();
        buildRichPipeline();
        settleWindow(w);
        w.flows()->showLinkMenu("videotestsrc_0", "src", w.mapToGlobal(QPoint(400, 300)));
        QTest::qWait(100);
        QMenu* menu = w.findChild<QMenu*>("linkMenu");
        QVERIFY(menu);
        save(menu, "ux_15_link_menu");
        menu->close();
        w.flows()->showElementMenu("videotestsrc_0", w.mapToGlobal(QPoint(400, 300)));
        QTest::qWait(100);
        menu = w.findChild<QMenu*>("elementMenu");
        QVERIFY(menu);
        save(menu, "ux_16_element_menu");
        menu->close();
    }

    void validatingAndSlow() {
        MainWindow w(c.get());
        w.show();
        buildRichPipeline();
        settleWindow(w);
        qint64 now = 0;
        c->slowWatch()->setClock([&now] { return now; });
        QVERIFY(c->editElement("videotestsrc_0", withProps({{"pattern", "ball"}, {"fake-slow", "8000"}})));
        QTest::qWait(500);
        QVERIFY(c->updating());
        now = 12000;
        c->slowWatch()->check();
        save(&w, "ux_17_validating");
        now = 61000;
        c->slowWatch()->check();
        QVERIFY(c->slowAsked());
        QTest::qWait(100);
        save(&w, "ux_18_slow_message");
        c->cancelUpdate();
        QVERIFY(settle(*c));
        QTest::qWait(300);
        w.scene()->rebuild();
        save(&w, "ux_19_cancelled");
    }

    void helperStopped() {
        MainWindow w(c.get());
        w.show();
        addSource(*c, "videotestsrc");
        addSource(*c, "audiotestsrc", withProps({{"fake-crash", "1"}}));
        settleWindow(w);
        save(&w, "ux_20_helper_stopped");
    }

    void setupPages() {
        struct Case {
            const char* name;
            const char* version;
            const char* noGst;
            const char* broken;
        };
        for (const Case& k : {Case{"ux_21_setup_not_installed", "1.20.3", "1", ""}, Case{"ux_22_setup_too_old", "1.14.5", "", ""},
                              Case{"ux_23_setup_helper_broken", "1.20.3", "", "1"}}) {
            qputenv("GPS_FAKE_GST_VERSION", k.version);
            qputenv("GPS_FAKE_NO_GST", k.noGst);
            qputenv("GPS_FAKE_BROKEN", k.broken);
            MainWindow w(c.get());
            w.show();
            QVERIFY(waitFor([&] { return w.showingSetup(); }));
            QTest::qWait(100);
            save(&w, k.name);
        }
        for (const char* name : {"GPS_FAKE_GST_VERSION", "GPS_FAKE_NO_GST", "GPS_FAKE_BROKEN"}) qunsetenv(name);
    }
};

QTEST_MAIN(ScreensTest)
#include "screens_test.moc"
