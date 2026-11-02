// screens_test.cpp – saves screenshots of the main window and the dialogs in key states to report/screenshots/.
// They are looked at by a human (or by Claude with the Read tool); the test only checks that they were written
// and are not blank.
#include <QDir>
#include <QMenu>
#include <QPushButton>
#include <QTabWidget>

#include "canvasscene.h"
#include "canvasview.h"
#include "capsdialog.h"
#include "confirmdialog.h"
#include "elementitem.h"
#include "flows.h"
#include "mainwindow.h"
#include "pickerdialog.h"
#include "propertydialog.h"
#include "testsupport.h"
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
        QDir().mkpath(GPS_SHOT_DIR);
        QImage image = widget->grab().toImage();
        QVERIFY2(image.width() > 100 && image.height() > 50, qPrintable(name));
        QSet<QRgb> colours;
        for (int y = 0; y < image.height(); y += 7) {
            for (int x = 0; x < image.width(); x += 7) colours.insert(image.pixel(x, y));
        }
        QVERIFY2(colours.size() > 3, qPrintable(name + " looks blank"));
        QVERIFY(image.save(QString("%1/%2.png").arg(GPS_SHOT_DIR, name)));
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
    void initTestCase() { Theme::apply(*qApp); }
    void init() { c = makeController(); }
    void cleanup() { c.reset(); }

    void emptyCanvas() {
        MainWindow w(c.get());
        w.show();
        QTest::qWait(200);
        save(&w, "01-bos-tuval");
    }

    void firstSource() {
        MainWindow w(c.get());
        w.show();
        addSource(*c, "videotestsrc", withProps({{"pattern", "snow"}, {"is-live", "true"}}));
        settleWindow(w);
        save(&w, "02-ilk-kaynak");
    }

    void completePipeline() {
        MainWindow w(c.get());
        w.show();
        buildRichPipeline();
        QVERIFY(c->result().isComplete());
        settleWindow(w);
        save(&w, "03-tamamlanmis-pipeline");
        w.view()->resetTransform();
        w.view()->centerOn(w.scene()->element("tee_1"));
        QTest::qWait(100);
        save(w.view(), "03b-tuval-1-e-1");
        w.bottomTabs()->setCurrentIndex(0);
        QTest::qWait(100);
        save(&w, "04-gunluk-paneli");
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
        save(&w, "05-sometimes-ve-bilinmeyen-veri");
    }

    void failedUpdate() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString conv = addAfter(*c, src, "src", "videoconvert");
        addAfter(*c, conv, "src", "fakesink", "sink", withProps({{"fake-error", "Could not open resource for writing: /readonly/out.mp4"}}));
        settleWindow(w);
        QVERIFY(!c->canSave());
        save(&w, "06-basarisiz-guncelleme");
    }

    void refusedLink() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        addAfter(*c, src, "src", "jpegdec");
        settleWindow(w);
        save(&w, "07-reddedilen-baglanti");
    }

    void crashedHelper() {
        MainWindow w(c.get());
        w.show();
        addSource(*c, "videotestsrc");
        QString bad = addSource(*c, "audiotestsrc", withProps({{"fake-crash", "1"}}));
        settleWindow(w);
        QCOMPARE(c->result().status, QString("crashed"));
        save(&w, "08-yardimci-coktu");
    }

    void dialogs() {
        MainWindow w(c.get());
        w.show();
        buildRichPipeline();
        settleWindow(w);

        PropertyDialogInput input;
        input.info = *need(*c, "videotestsrc");
        input.title = "Kaynak ekle: videotestsrc";
        input.props = {{"pattern", "ball"}};
        input.exposed = {"num-buffers"};
        PropertyDialog properties(input);
        properties.show();
        QTest::qWait(100);
        save(&properties, "10-property-dialog");

        PropertyDialogInput tee;
        tee.info = *need(*c, "propdemo");
        tee.title = "Property düzenle: propdemo_0";
        tee.props = {{"p-int", "42"}, {"p-flags", "a+c"}};
        PropertyDialog all(tee);
        all.resize(700, 760);
        all.show();
        QTest::qWait(100);
        save(&all, "11-property-dialog-tum-turler");

        CapsDialogInput caps;
        caps.title = "Caps düzenle";
        caps.subtitle = "videoconvert_4.src  →  compositor_3.sink_%u";
        caps.allowed = need(*c, "videoconvert")->padTemplate("src", "src")->structures;
        caps.current = "video/x-raw, format=(string)NV12, width=(int)640";
        caps.padProperties = need(*c, "compositor")->padTemplate("sink_%u", "sink")->padProperties;
        caps.padProps = {{"xpos", "320"}};
        CapsDialog capsDialog(caps);
        capsDialog.show();
        QTest::qWait(100);
        save(&capsDialog, "12-caps-dialog");

        PickerData picker;
        picker.title = "Seçici";
        QString free = addSource(*c, "videotestsrc");
        picker.subtitle = free + ".src   ·   video/x-raw";
        picker.existing = c->existingTargets(free, "src");
        QStringList compatible;
        c->fetchCompatible("video/x-raw", [&](const QStringList& l) { compatible = l; });
        QVERIFY(waitFor([&] { return !compatible.isEmpty(); }));
        picker.compatible = compatible;
        for (const ListEntry& e : c->catalog()) {
            if (!e.isSource) picker.candidates << e;
        }
        PickerDialog pick(picker);
        pick.show();
        QTest::qWait(100);
        save(&pick, "13-secici");

        ConfirmDialog confirm("Silinecekler", "Şu 3 element silinecek (3 bağlantıyla birlikte)",
                              {"nvvideoconvert_5   (nvvideoconvert)", "textoverlay_6   (textoverlay)", "fakesink_7   (fakesink)"}, "Kaldır");
        confirm.show();
        QTest::qWait(100);
        save(&confirm, "14-onay");
    }

    void menusAndSourcePanel() {
        MainWindow w(c.get());
        w.show();
        buildRichPipeline();
        settleWindow(w);
        w.flows()->showLinkMenu("videotestsrc_0", "src", w.mapToGlobal(QPoint(400, 300)));
        QTest::qWait(100);
        QMenu* menu = w.findChild<QMenu*>("linkMenu");
        QVERIFY(menu);
        save(menu, "15-baglanti-menusu");
        menu->close();
        w.flows()->showElementMenu("videotestsrc_0", w.mapToGlobal(QPoint(400, 300)));
        QTest::qWait(100);
        menu = w.findChild<QMenu*>("elementMenu");
        QVERIFY(menu);
        save(menu, "16-element-menusu");
        menu->close();
    }

    void updatingState() {
        MainWindow w(c.get());
        w.show();
        buildRichPipeline();
        settleWindow(w);
        QVERIFY(c->editElement("videotestsrc_0", withProps({{"pattern", "ball"}, {"fake-slow", "1500"}})));
        QTest::qWait(400);
        QVERIFY(c->updating());
        save(&w, "17-guncelleniyor");
        QVERIFY(settle(*c));
    }
};

QTEST_MAIN(ScreensTest)
#include "screens_test.moc"
