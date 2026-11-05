// integration_test.cpp – the real GUI (offscreen) driven like a user, against the REAL gps-inspect / gps-update / gps-run.
// Nothing here is faked except the two helpers that crash or hang on purpose (written to a temp folder).
// Every flow of SPEC 8 goes through widgets: toolbar, canvas clicks, the drawer pages (picker, properties, caps), menus.
// File dialogs are not driven (QFileDialog is native); the window's saveFile()/openFile() are called instead.
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <cstdlib>

#include "canvasmetrics.h"
#include "canvasscene.h"
#include "canvasview.h"
#include "capspage.h"
#include "confirmdialog.h"
#include "drawer.h"
#include "elementitem.h"
#include "factorydelegate.h"
#include "flows.h"
#include "linkitem.h"
#include "logpanel.h"
#include "mainwindow.h"
#include "paditem.h"
#include "pickerpage.h"
#include "propertypage.h"
#include "testsupport.h"
#include "theme.h"
#include "updatepanel.h"

using namespace support;

// A failed check must leave the helper, then the test slot. STEP does the second part.
#define STEP(call)                                \
    do {                                          \
        call;                                     \
        if (QTest::currentTestFailed()) return;   \
    } while (0)
#define NEED(cond) \
    do { if (!QTest::qVerify(static_cast<bool>(cond), #cond, "", __FILE__, __LINE__)) return; } while (0)
#define NEED_R(cond, ret) \
    do { if (!QTest::qVerify(static_cast<bool>(cond), #cond, "", __FILE__, __LINE__)) return ret; } while (0)

namespace {

struct Timing {
    QString step;
    double uiMs;      // from the click on Save / Add element (or the menu action) until the canvas shows the new result
    double helperMs;  // the helper's own "ms" in its JSON
};
QList<Timing> g_timings;

QString text(const std::string& s) { return QString::fromStdString(s); }

QString sh(const QString& s) { return "'" + QString(s).replace("'", "'\\''") + "'"; }

}  // namespace

class IntegrationTest : public QObject {
    Q_OBJECT

    std::unique_ptr<Controller> c;
    std::unique_ptr<MainWindow> w;
    QTemporaryDir work;   // fixtures, saved plans, outputs
    QString avi;          // av.avi: jpeg video + raw audio, 1 s
    QByteArray goodBin;   // the real helpers' folder

    // ------------------------------------------------------------------ waiting and finding

    // A drawer page is found by asking the drawer; menus and the confirmation by looking for a visible child.
    template <class T>
    T* visibleChild() {
        if constexpr (std::is_same_v<T, PropertyPage>) {
            return w->flows()->propertyPage();
        } else if constexpr (std::is_same_v<T, CapsPage>) {
            return w->flows()->capsPage();
        } else if constexpr (std::is_base_of_v<DrawerPage, T>) {
            return w->drawer()->page<T>();
        } else {
            for (T* d : w->findChildren<T*>()) {
                if (d->isVisible()) return d;
            }
            return nullptr;
        }
    }
    template <class T>
    T* shown(int ms = 8000) {
        T* found = nullptr;
        return QTest::qWaitFor([&] { return (found = visibleChild<T>()) != nullptr; }, ms) ? found : nullptr;
    }
    template <class W>
    static W* in(QWidget* dialog, const QString& objectName) {
        auto* row = dialog->findChild<QWidget*>(objectName);
        return row ? row->findChild<W*>() : nullptr;
    }
    static void press(QWidget* dialog, const char* button) {
        auto* b = dialog->findChild<QPushButton*>(button);
        QVERIFY2(b, button);
        QTest::mouseClick(b, Qt::LeftButton);
    }

    // The update is over and the canvas shows it.
    void idle() {
        QVERIFY(waitFor([&] { return !c->busy(); }, 30000));
        QTest::qWait(30);
        w->scene()->rebuild();
        checkLayout();
    }

    // After every step of every flow: no two boxes overlap, and every link runs from left to right
    // (a box that sits left of its source makes the link curl back over the boxes).
    void checkLayout() {
        const gps::Plan& plan = c->plan();
        for (size_t i = 0; i < plan.elements.size(); ++i) {
            QRectF a(plan.elements[i].x, plan.elements[i].y, metrics::kBoxWidth, c->view(text(plan.elements[i].name)).height());
            for (size_t j = i + 1; j < plan.elements.size(); ++j) {
                QRectF b(plan.elements[j].x, plan.elements[j].y, metrics::kBoxWidth, c->view(text(plan.elements[j].name)).height());
                QVERIFY2(!a.intersects(b), qPrintable(QString("boxes overlap: %1 and %2").arg(text(plan.elements[i].name), text(plan.elements[j].name))));
            }
        }
        for (const gps::PlanLink& l : plan.links) {
            const gps::PlanElement* from = plan.find(l.srcElement);
            const gps::PlanElement* to = plan.find(l.sinkElement);
            QVERIFY2(from->x + metrics::kBoxWidth <= to->x,
                     qPrintable(QString("link runs backwards: %1 (x=%2) -> %3 (x=%4)").arg(text(l.srcElement)).arg(from->x).arg(text(l.sinkElement)).arg(to->x)));
        }
    }

    void fit() {
        w->scene()->rebuild();
        w->view()->fitAll();
        QCoreApplication::processEvents();
    }

    QPoint at(const QPointF& scenePoint) { return w->view()->mapFromScene(scenePoint); }

    // Runs `action` (a click that starts an update) and records how long the canvas needed to show the answer.
    void timed(const QString& step, const std::function<void()>& action) {
        QElapsedTimer clock;
        bool finished = false;
        qint64 shownAt = -1;
        auto k1 = connect(c.get(), &Controller::updateFinished, this, [&] { finished = true; });
        auto k2 = connect(w->scene(), &CanvasScene::rebuilt, this, [&] {
            if (finished && shownAt < 0) shownAt = clock.elapsed();
        });
        clock.start();
        action();
        (void)QTest::qWaitFor([&] { return shownAt >= 0; }, 30000);
        disconnect(k1);
        disconnect(k2);
        QVERIFY2(shownAt >= 0, qPrintable("no canvas update after: " + step));
        g_timings.append({step, double(shownAt), c->result().ms});
        qInfo().noquote() << QString("TIMING %1: %2 ms until the canvas (helper said %3 ms)").arg(step).arg(shownAt).arg(c->result().ms, 0, 'f', 1);
    }

    bool shot(const QString& name) {
        fit();
        QTest::qWait(250);
        QDir().mkpath(shotDir());
        return w->grab().save(QString("%1/ux_int_%2.png").arg(shotDir(), name));
    }
    static bool shotOf(QWidget* widget, const QString& name) {
        QTest::qWait(150);
        QDir().mkpath(shotDir());
        return widget->grab().save(QString("%1/ux_int_%2.png").arg(shotDir(), name));
    }

    // ------------------------------------------------------------------ pixels

    // The whole scene as an image (scene units = pixels). `area` is the scene rectangle that was drawn.
    QImage renderScene(QRectF* area) {
        w->scene()->rebuild();
        *area = w->scene()->itemsBoundingRect().adjusted(-20, -20, 20, 20);
        QImage image(area->size().toSize(), QImage::Format_ARGB32);
        image.fill(Qt::black);
        QPainter painter(&image);
        w->scene()->render(&painter, QRectF(QPointF(0, 0), area->size()), *area);
        painter.end();
        return image;
    }
    static bool near(const QColor& a, const QColor& b, int tolerance = 30) {
        return qAbs(a.red() - b.red()) < tolerance && qAbs(a.green() - b.green()) < tolerance && qAbs(a.blue() - b.blue()) < tolerance;
    }
    // Is the pad circle of this pad drawn in its own colour at its centre? (Something on top would change the pixel.)
    bool padIsDrawn(const QImage& image, const QRectF& area, const QString& element, const QString& identity, bool isSrc, bool linked) {
        PadItem* pad = w->scene()->pad(element, identity, isSrc);
        if (!pad) return false;
        QColor want = !linked ? Theme::background() : isSrc ? Theme::accent() : Theme::muted();
        QColor ring = isSrc ? Theme::accent() : Theme::muted();
        QPointF centre = pad->center() - area.topLeft();
        // the ring is 2 px wide at radius 6: sample 6 px to the right of the centre as well
        return near(image.pixelColor(centre.toPoint()), want) && near(image.pixelColor((centre + QPointF(6, 0)).toPoint()), ring);
    }

    // ------------------------------------------------------------------ canvas and menus

    void clickPad(const QString& element, const QString& identity, bool isSrc, Qt::MouseButton button = Qt::LeftButton) {
        fit();
        PadItem* pad = w->scene()->pad(element, identity, isSrc);
        QVERIFY2(pad, qPrintable(element + "." + identity));
        QTest::mouseClick(w->view()->viewport(), button, Qt::NoModifier, at(pad->center()));
    }
    void rightClickElement(const QString& element) {
        fit();
        ElementItem* item = w->scene()->element(element);
        QVERIFY2(item, qPrintable(element));
        QTest::mouseClick(w->view()->viewport(), Qt::RightButton, Qt::NoModifier, at(item->pos() + QPointF(120, 12)));
    }
    void doubleClickElement(const QString& element) {
        fit();
        ElementItem* item = w->scene()->element(element);
        QVERIFY2(item, qPrintable(element));
        QPoint p = at(item->pos() + QPointF(120, 12));
        QTest::mouseClick(w->view()->viewport(), Qt::LeftButton, Qt::NoModifier, p);
        QTest::mouseDClick(w->view()->viewport(), Qt::LeftButton, Qt::NoModifier, p);
    }
    // Triggers an action of the open menu; the menu may close by itself when the action opens a dialog.
    void choose(const char* action) {
        QPointer<QMenu> menu;
        (void)QTest::qWaitFor([&] { return (menu = visibleChild<QMenu>()) != nullptr; }, 5000);
        QVERIFY2(menu, "no menu is open");
        QAction* a = menu->findChild<QAction*>(action);
        QVERIFY2(a, action);
        a->trigger();
        if (menu) menu->close();
    }
    void linkMenu(const QString& srcElement, const QString& identity, const char* action) {
        STEP(clickPad(srcElement, identity, true, Qt::RightButton));
        STEP(choose(action));
    }

    // ------------------------------------------------------------------ dialogs

    // Selects a row of the open picker page by its exact text and presses "Choose".
    void pickRow(PickerPage* picker, const QString& rowText, const QString& hint = QString()) {
        auto* list = picker->findChild<QListWidget*>("pickerList");
        QListWidgetItem* row = nullptr;
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->text() == rowText) row = list->item(i);
        }
        QVERIFY2(row, qPrintable("no row " + rowText));
        if (!hint.isEmpty()) QCOMPARE(row->data(HintRole).toString(), hint);
        list->setCurrentItem(row);
        press(picker, "saveButton");
    }

    static void setText(PropertyPage* d, const QString& property, const QString& value) {
        auto* line = in<QLineEdit>(d, "editor_" + property);
        QVERIFY2(line, qPrintable(property));
        line->clear();
        QTest::keyClicks(line, value);
    }
    static void setNumber(PropertyPage* d, const QString& property, int value) {
        auto* spin = in<QSpinBox>(d, "editor_" + property);
        QVERIFY2(spin, qPrintable(property));
        spin->setValue(value);
    }
    static void setChoice(PropertyPage* d, const QString& property, const QString& nick) {
        auto* combo = in<QComboBox>(d, "editor_" + property);
        QVERIFY2(combo, qPrintable(property));
        int i = combo->findData(nick);
        QVERIFY2(i >= 0, qPrintable(property + "=" + nick));
        combo->setCurrentIndex(i);
    }
    static void expose(PropertyPage* d, const QString& property) {
        auto* box = d->findChild<QCheckBox*>("expose_" + property);
        QVERIFY2(box, qPrintable(property));
        box->click();
    }

    static void selectMedia(CapsPage* d, const QString& media) {
        auto* combo = d->findChild<QComboBox*>("mediaCombo");
        int i = combo->findText(media);
        QVERIFY2(i >= 0, qPrintable("no media " + media));
        combo->setCurrentIndex(i);
    }
    // A list field takes the value from its combo; a range field is ticked and gets the number.
    static void pinField(CapsPage* d, const QString& field, const QString& value) {
        // every structure has its own page and may have a field of this name: take the one on the visible page
        QWidget* row = nullptr;
        for (QWidget* candidate : d->findChildren<QWidget*>("field_" + field)) {
            if (candidate->isVisibleTo(d)) row = candidate;
        }
        QVERIFY2(row, qPrintable("no visible field " + field));
        if (auto* combo = row->findChild<QComboBox*>()) {
            int i = combo->findData(value);
            QStringList items;
            for (int k = 0; k < combo->count(); ++k) items << combo->itemData(k).toString();
            QVERIFY2(i >= 0, qPrintable(field + "=" + value + " in " + items.join(",")));
            combo->setCurrentIndex(i);
            return;
        }
        row->findChild<QCheckBox*>()->click();
        if (auto* spin = row->findChild<QSpinBox*>()) spin->setValue(value.toInt());
        else if (auto* real = row->findChild<QDoubleSpinBox*>()) real->setValue(value.toDouble());
        else if (auto* line = row->findChild<QLineEdit*>()) line->setText(value);
    }

    // A CapsPage that opened (or null). It opens as soon as an update ends, so a short wait is enough.
    CapsPage* capsPage(int ms = 1500) { return shown<CapsPage>(ms); }

    // ------------------------------------------------------------------ whole flows

    // Add source (toolbar) -> picker page -> properties page -> Add element. Returns the new element's name.
    QString addSource(const QString& factory, const std::function<void(PropertyPage*)>& fill = {}, const QString& step = QString()) {
        w->findChild<QAction*>("actionAddSource")->trigger();
        auto* picker = shown<PickerPage>();
        NEED_R(picker, QString());
        pickRow(picker, factory);
        auto* dialog = shown<PropertyPage>();
        NEED_R(dialog, QString());
        if (fill) fill(dialog);
        if (QTest::currentTestFailed()) return QString();
        timed(step.isEmpty() ? "Add source: " + factory : step, [&] { press(dialog, "saveButton"); });
        if (QTest::currentTestFailed()) return QString();
        idle();
        NEED_R(!c->plan().elements.empty(), QString());
        return text(c->plan().elements.back().name);
    }

    // Free src pad -> picker page -> a new factory -> properties page -> Add element -> (caps page). Returns the new element's name.
    // capsFill == null: the caps page is applied with everything on Auto. capsFill given: it fills the page, then Apply.
    QString addBehind(const QString& element, const QString& identity, const QString& factory,
                      const std::function<void(PropertyPage*)>& fill = {}, const std::function<void(CapsPage*)>& capsFill = {}) {
        clickPad(element, identity, true);
        auto* picker = shown<PickerPage>();
        NEED_R(picker, QString());
        pickRow(picker, factory);
        auto* dialog = shown<PropertyPage>();
        NEED_R(dialog, QString());
        if (fill) fill(dialog);
        if (QTest::currentTestFailed()) return QString();
        timed("Add element: " + factory + " (behind a pad)", [&] { press(dialog, "saveButton"); });
        if (QTest::currentTestFailed()) return QString();
        idle();
        QString created = text(c->plan().elements.back().name);
        if (auto* caps = capsPage()) {
            if (capsFill) capsFill(caps);
            if (QTest::currentTestFailed()) return QString();
            timed("Apply caps: " + created, [&] { press(caps, "saveButton"); });
            if (QTest::currentTestFailed()) return QString();
            idle();
        }
        return created;
    }

    // Free src pad -> picker page -> an existing element's row ("Request a pad" or a free sink) -> (CapsPage, saved as is).
    void linkTo(const QString& element, const QString& identity, const QString& rowText) {
        clickPad(element, identity, true);
        auto* picker = shown<PickerPage>();
        NEED(picker);
        timed("Link to an existing element: " + rowText, [&] { pickRow(picker, rowText); });
        idle();
        if (auto* caps = capsPage()) {
            timed("Apply caps (Auto): " + rowText, [&] { press(caps, "saveButton"); });
            idle();
        }
    }

    // The links of the plan as comparable text, ignoring element positions and the plan's name.
    QString linkText() const {
        QStringList out;
        for (const gps::PlanLink& l : c->plan().links) {
            out << text(l.srcElement + "." + l.srcPad + ">" + l.sinkElement + "." + l.sinkPad + "[" + l.caps + "]");
        }
        return out.join("\n");
    }

    int padsFree(const QString& element) const {
        int n = 0;
        for (const PadView& p : c->view(element).srcs) n += p.state == PadView::Free;
        return n;
    }

    // filesrc ! avidemux, video -> jpegdec -> videoconvert -> fakesink, audio -> fakesink. Returns false when a step failed.
    struct AviPipeline {
        QString file, demux, dec, queue, conv, vsink, asink;
    };
    AviPipeline buildAvi(bool complete) {
        AviPipeline p;
        p.file = addSource("filesrc", [&](PropertyPage* d) { setText(d, "location", avi); });
        if (QTest::currentTestFailed()) return p;
        p.demux = addBehind(p.file, "src", "avidemux");
        if (QTest::currentTestFailed()) return p;
        if (!complete) return p;
        p.dec = addBehind(p.demux, "video_%u#0", "jpegdec");
        if (QTest::currentTestFailed()) return p;
        // jpegdec directly into videoconvert fails in this GStreamer (see demuxSometimesPads...), a queue between them works
        p.queue = addBehind(p.dec, "src", "queue");
        if (QTest::currentTestFailed()) return p;
        p.conv = addBehind(p.queue, "src", "videoconvert");
        if (QTest::currentTestFailed()) return p;
        p.vsink = addBehind(p.conv, "src", "fakesink");
        if (QTest::currentTestFailed()) return p;
        p.asink = addBehind(p.demux, "audio_%u#0", "fakesink");
        return p;
    }

    // A helper folder: the real gps-inspect / gps-update / gps-run linked in, one of them replaced by a script.
    QString helperDirWith(const QString& name, const QString& script) {
        auto* dir = new QTemporaryDir;  // lives until the process ends; the folder is small
        for (const char* tool : {"gps-inspect", "gps-update", "gps-run"}) {
            if (name == tool) continue;
            QFile::link(QString::fromUtf8(goodBin) + "/" + tool, dir->filePath(tool));
        }
        QFile f(dir->filePath(name));
        if (!f.open(QIODevice::WriteOnly)) return QString();
        f.write(script.toUtf8());
        f.close();
        f.setPermissions(QFile::permissions(f.fileName()) | QFile::ExeOwner | QFile::ExeUser | QFile::ExeGroup);
        return dir->path();
    }

private slots:
    void initTestCase() {
        isolateSettings();
        Theme::apply(*qApp);
        goodBin = qgetenv("GPS_BIN_DIR");
        if (goodBin.isEmpty()) QSKIP("GPS_BIN_DIR is not set");
        QVERIFY(work.isValid());
        QString cmd = sh(GPS_FIXTURE_SCRIPT) + " " + sh(work.path()) + " >/dev/null 2>&1";
        QCOMPARE(std::system(qPrintable(cmd)), 0);
        avi = work.filePath("av.avi");
        QVERIFY(QFile::exists(avi));
        qInfo() << "helpers:" << goodBin << " fixtures:" << work.path();
    }

    void init() {
        qputenv("GPS_BIN_DIR", goodBin);
        c = std::make_unique<Controller>();
        c->setUpdateTimeoutMs(10000);
        c->loadCatalog();
        QVERIFY(waitFor([&] { return c->catalogLoaded(); }, 30000));
        w = std::make_unique<MainWindow>(c.get());
        w->show();
    }
    void cleanup() {
        w.reset();
        c.reset();
        qputenv("GPS_BIN_DIR", goodBin);
    }

    void cleanupTestCase() {
        if (g_timings.isEmpty()) return;
        QDir().mkpath(shotDir());
        QFile f(QString("%1/ux_int_timings.md").arg(shotDir()));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
        QTextStream out(&f);
        out << "# Integration timings (real helpers, GPU image)\n\n";
        out << "From the click on Add element / Save (or the menu action) until the canvas shows the new result, in ms. "
               "The helper column is the time gps-update reports itself; the difference is process start, JSON, saving the plan and drawing.\n\n";
        out << "| Step | Until the canvas (ms) | Helper (ms) |\n|---|---:|---:|\n";
        for (const Timing& t : g_timings) out << "| " << t.step << " | " << int(t.uiMs) << " | " << QString::number(t.helperMs, 'f', 1) << " |\n";
    }

    // =================================================================================================================
    // 1. The catalog the GUI gets from the real gps-inspect
    // =================================================================================================================

    void realCatalogFillsTheAddSourcePicker() {
        QVERIFY2(c->catalog().size() > 300, qPrintable(QString::number(c->catalog().size())));
        w->findChild<QAction*>("actionAddSource")->trigger();
        auto* picker = shown<PickerPage>();
        QVERIFY(picker);
        QVERIFY(w->drawer()->isOpen());
        auto* list = picker->findChild<QListWidget*>("pickerList");
        QStringList names;
        int nvidia = 0;
        for (int i = 1; i < list->count(); ++i) {  // row 0 is the "Sources" header
            names << list->item(i)->text();
            nvidia += list->item(i)->data(NvidiaRole).toBool();
        }
        for (const char* f : {"videotestsrc", "filesrc", "audiotestsrc", "v4l2src"}) QVERIFY2(names.contains(f), f);
        QVERIFY(!names.contains("fakesink"));
        bool deepstream = false;  // a plain Ubuntu has no nv* elements, so no badge is expected there
        for (const ListEntry& e : c->catalog()) deepstream = deepstream || e.nvidia;
        if (deepstream) QVERIFY2(nvidia > 0, "no NVIDIA badge among the sources");
        qInfo() << "factories" << c->catalog().size() << "sources" << names.size() << "nvidia sources" << nvidia;
        QVERIFY(waitFor([&] { return w->drawer()->width() == Drawer::kWidth; }, 3000));
        QVERIFY(shotOf(w->drawer(), "00_add_source_picker"));
        press(picker, "cancelButton");
        QVERIFY(!w->drawer()->isOpen());
    }

    // =================================================================================================================
    // 2. filesrc ! avidemux: Sometimes pads appear after Save; video pad -> jpegdec; the whole pipeline runs
    // =================================================================================================================

    void demuxSometimesPadsAppearAndTheWholePipelineRuns() {
        QString file = addSource("filesrc", [&](PropertyPage* d) { setText(d, "location", avi); });
        QVERIFY(!file.isEmpty());
        QVERIFY2(c->result().ok(), qPrintable(c->result().message));
        QCOMPARE(w->scene()->pad(file, "src", true)->view().state, PadView::Free);
        // filesrc's own pad has no caps; the picker's caps come from the temporary end (video/x-msvideo)
        QCOMPARE(c->view(file).srcs[0].media, QString("video/x-msvideo"));

        QString demux = addBehind(file, "src", "avidemux");
        QVERIFY(!demux.isEmpty());
        QVERIFY2(c->result().ok(), qPrintable(c->result().message));
        // Sometimes pads exist only in the update result; the Plan has no link from them yet
        QVERIFY(c->plan().linkFrom(demux.toStdString(), "video_%u#0") < 0);
        QCOMPARE(c->view(demux).srcs.size(), 2);
        PadItem* video = w->scene()->pad(demux, "video_%u#0", true);
        PadItem* audio = w->scene()->pad(demux, "audio_%u#0", true);
        QVERIFY(video && audio);
        QCOMPARE(video->view().state, PadView::Free);
        QCOMPARE(video->view().media, QString("image/jpeg"));
        QCOMPARE(audio->view().media, QString("audio/x-raw"));
        QVERIFY(video->view().label.startsWith("video_"));
        QVERIFY(!c->result().isComplete());
        QVERIFY(!c->canRun());
        QVERIFY(c->canSave());
        QVERIFY(w->findChild<QAction*>("actionSave")->isEnabled());
        QVERIFY(!w->findChild<QAction*>("actionRun")->isEnabled());
        // the Validation panel shows exactly the pads of the JSON
        QCOMPARE(w->updatePanel()->table()->rowCount(), c->result().pads.size());
        QVERIFY(shot("01_demux_sometimes_pad"));

        // video_%u#0 -> jpegdec: the picker must rank jpegdec as compatible (its sink takes image/jpeg)
        clickPad(demux, "video_%u#0", true);
        auto* picker = shown<PickerPage>();
        QVERIFY(picker);
        QVERIFY(shotOf(w->drawer(), "02_picker_jpeg_pad"));
        auto* list = picker->findChild<QListWidget*>("pickerList");
        QStringList firstRows;
        for (int i = 0; i < list->count() && firstRows.size() < 40; ++i) {
            if (list->item(i)->data(HintRole).toString() == "compatible") firstRows << list->item(i)->text();
        }
        QVERIFY2(firstRows.contains("jpegdec"), qPrintable(firstRows.join(",")));
        QVERIFY2(!firstRows.contains("videoconvert"), "raw video converter offered for image/jpeg");
        pickRow(picker, "jpegdec", "compatible");
        auto* props = shown<PropertyPage>();
        QVERIFY(props);
        QVERIFY(w->flows()->propertiesTitle().contains("jpegdec"));
        timed("Add element: jpegdec (behind a Sometimes pad)", [&] { press(props, "saveButton"); });
        idle();
        QVERIFY2(c->result().ok(), qPrintable(c->result().message));
        // the same Sometimes pad must have been linked by the second update (Plan persists, pads come back)
        QCOMPARE(c->plan().links.size(), size_t(2));
        QCOMPARE(text(c->plan().links[1].srcPad), QString("video_%u#0"));
        auto* caps = capsPage();
        QVERIFY(caps);
        QCOMPARE(caps->findChild<QComboBox*>("mediaCombo")->itemText(1), QString("image/jpeg"));
        QVERIFY(shotOf(w->bottomTabs(), "03_caps_fixed_fields"));
        timed("Apply caps (Auto)", [&] { press(caps, "saveButton"); });
        idle();
        QString dec = text(c->plan().elements.back().name);
        QCOMPARE(c->view(dec).srcs[0].media, QString("video/x-raw"));
        QCOMPARE(c->view(demux).srcs.size(), 2);
        QCOMPARE(padsFree(demux), 1);  // audio only

        // jpegdec straight into videoconvert: this GStreamer (1.20.3) fails to decode ("Improper call to JPEG library").
        // The same happens with gst-launch, so the GUI must show it as an error on jpegdec and keep the change.
        QString conv = addBehind(dec, "src", "videoconvert");
        QVERIFY(!conv.isEmpty());
        qInfo() << "jpegdec -> videoconvert: status" << c->result().status << "element" << c->result().errorElement << c->result().message;
        // GStreamer 1.20 fails here (a libjpeg problem), newer ones decode fine: the failure path is checked when it happens.
        if (c->result().status == "error") {
            QCOMPARE(c->result().errorElement, dec);
            QVERIFY(!c->canSave());
            QVERIFY(w->scene()->element(dec)->view().isError);
            QVERIFY(shot("03b_jpegdec_problem_on_element"));
            // the user's way out: Insert queue on that link
            timed("Insert queue (after the jpegdec -> videoconvert problem)", [&] { linkMenu(dec, "src", "actionInsertQueue"); });
            idle();
        }
        QVERIFY2(c->result().ok(), qPrintable(c->result().status + ": " + c->result().message));
        QVERIFY(c->canSave());

        QString vsink = addBehind(conv, "src", "fakesink");
        QString asink = addBehind(demux, "audio_%u#0", "fakesink");
        QVERIFY(!vsink.isEmpty() && !asink.isEmpty());
        // Both demuxer pads end in a sink: what does the real update say about a demuxer without queues?
        qInfo() << "demux -> two sinks: status" << c->result().status << "ms" << c->result().ms << "message" << c->result().message;
        if (!c->result().isComplete()) {
            QVERIFY(shot("03c_demux_two_sinks_no_queue"));
            timed("Insert queue (audio branch)", [&] { linkMenu(demux, "audio_%u#0", "actionInsertQueue"); });
            idle();
            qInfo() << "after a queue on the audio branch only: status" << c->result().status << c->result().message;
            if (!c->result().isComplete()) {
                timed("Insert queue (video branch)", [&] { linkMenu(demux, "video_%u#0", "actionInsertQueue"); });
                idle();
            }
        }
        QVERIFY2(c->result().isComplete(), qPrintable(c->result().status + ": " + c->result().message + " | " + c->result().missing.join(";") + " | " + c->result().refused.join(";")));
        QVERIFY(c->canRun());
        QVERIFY(shot("04_avi_pipeline_complete"));

        // Save, then Run through the toolbar
        QString conf = work.filePath("avi.conf");
        QVERIFY(w->saveFile(conf));
        QVERIFY(w->findChild<QLabel*>("commandLabel")->text().contains("gst-launch-1.0 avi"));
        QSignalSpy ended(c.get(), &Controller::runFinished);
        QElapsedTimer clock;
        clock.start();
        w->findChild<QAction*>("actionRun")->trigger();
        QCOMPARE(w->findChild<QAction*>("actionRun")->text(), QString("&Stop"));
        QVERIFY(waitFor([&] { return ended.count() == 1; }, 30000));
        qInfo() << "TIMING Run (avi pipeline):" << clock.elapsed() << "ms, result" << ended[0][0].toString();
        g_timings.append({"Run: filesrc ! avidemux ! jpegdec ! videoconvert ! fakesink (+ audio)", double(clock.elapsed()), 0});
        QCOMPARE(ended[0][0].toString(), QString("eos"));
        QVERIFY(w->logPanel()->toPlainText().contains("Run finished: eos"));
    }

    // =================================================================================================================
    // 3. tee with 2 Request src pads; queue insert / remove
    // =================================================================================================================

    void teeTwoRequestPadsAndQueueInsertRemove() {
        QString src = addSource("videotestsrc", [&](PropertyPage* d) { setNumber(d, "num-buffers", 20); });
        QVERIFY(!src.isEmpty());
        QString tee = addBehind(src, "src", "tee", [&](PropertyPage* d) {
            auto* pads = d->findChild<QSpinBox*>("padCount_src_%u");
            QVERIFY(pads);
            QCOMPARE(pads->value(), 1);  // all templates Request: the default is 1
            pads->setValue(2);
        });
        QVERIFY(!tee.isEmpty());
        QVERIFY2(c->result().ok(), qPrintable(c->result().message));
        QCOMPARE(c->plan().find(tee.toStdString())->requestSrc.at("src_%u"), 2);
        QCOMPARE(c->view(tee).srcs.size(), 2);
        QCOMPARE(w->scene()->pad(tee, "src_%u#0", true)->view().state, PadView::Free);
        QCOMPARE(w->scene()->pad(tee, "src_%u#1", true)->view().state, PadView::Free);
        QVERIFY(w->scene()->pad(tee, "src_%u#0", true)->view().label.startsWith("src_"));
        QVERIFY(shot("05_tee_two_request_pads"));

        QString q0 = addBehind(tee, "src_%u#0", "queue");
        QString sink0 = addBehind(q0, "src", "fakesink");
        QString sink1 = addBehind(tee, "src_%u#1", "fakesink");  // straight into a sink, no queue
        QVERIFY(!sink0.isEmpty() && !sink1.isEmpty());
        QVERIFY2(c->result().isComplete(), qPrintable(c->result().status + ": " + c->result().message));
        QString before = linkText();
        size_t elementsBefore = c->plan().elements.size();

        // Insert queue on tee.src_%u#1 -> sink1
        timed("Insert queue", [&] { linkMenu(tee, "src_%u#1", "actionInsertQueue"); });
        idle();
        QCOMPARE(c->plan().elements.size(), elementsBefore + 1);
        QVERIFY2(c->result().isComplete(), qPrintable(c->result().status + ": " + c->result().message));
        QString queue = text(c->plan().elements.back().name);
        QCOMPARE(text(c->plan().find(queue.toStdString())->factory), QString("queue"));
        QVERIFY(w->scene()->element(queue));
        QCOMPARE(c->plan().linkFrom(tee.toStdString(), "src_%u#1") >= 0, true);
        QVERIFY(shot("06_queue_inserted"));

        // Remove queue through the element menu: the links are as before
        timed("Remove queue", [&] {
            rightClickElement(queue);
            choose("actionRemoveQueue");
        });
        idle();
        QCOMPARE(c->plan().elements.size(), elementsBefore);
        QCOMPARE(linkText(), before);
        QVERIFY2(c->result().isComplete(), qPrintable(c->result().status + ": " + c->result().message));
    }

    // =================================================================================================================
    // 4. compositor: 'Request a pad' from two branches, caps choice from the real structures, xpos on the Request pad
    // =================================================================================================================

    void compositorTwoBranchesCapsAndPadSettings() {
        QString a = addSource("videotestsrc", [&](PropertyPage* d) {
            setNumber(d, "num-buffers", 30);
            setChoice(d, "pattern", "ball");
        });
        QVERIFY(!a.isEmpty());

        // The real allowedStructures of videotestsrc.src drive the dialog
        QStringList seenFormats;
        QString mix = addBehind(a, "src", "compositor", {}, [&](CapsPage* d) {
            QVERIFY(shotOf(w->bottomTabs(), "07_caps_page_real_fields"));
            QVERIFY(w->flows()->capsTitle().startsWith("Caps ·"));
            auto* media = d->findChild<QComboBox*>("mediaCombo");
            QVERIFY2(media->findText("video/x-raw") >= 0, qPrintable(media->itemText(media->count() - 1)));
            selectMedia(d, "video/x-raw");
            auto* format = d->findChild<QWidget*>("field_format")->findChild<QComboBox*>();
            QVERIFY(format);
            for (int i = 1; i < format->count(); ++i) seenFormats << format->itemData(i).toString();
            QVERIFY2(seenFormats.contains("I420") && seenFormats.contains("RGBA"), qPrintable(seenFormats.join(",")));
            pinField(d, "format", "I420");
            pinField(d, "width", "160");
            pinField(d, "height", "120");
            // compositor's sink pad has its own settings
            QVERIFY(d->findChild<QWidget*>("padSettings")->isVisible());
            QVERIFY(d->findChild<QWidget*>("padEditor_xpos"));
            QVERIFY(d->findChild<QWidget*>("padEditor_alpha"));
        });
        QVERIFY(!mix.isEmpty());
        QVERIFY2(c->result().ok(), qPrintable(c->result().message));
        // the fields come in the order of the structure the tool reported; the content is what matters
        QString choice = text(c->plan().links[0].caps);
        QVERIFY2(choice.startsWith("video/x-raw, ") && choice.contains("format=(string)I420") && choice.contains("width=(int)160") &&
                     choice.contains("height=(int)120"), qPrintable(choice));
        const PadReport* real = c->result().pad(a, "src", true);
        QVERIFY(real);
        QVERIFY2(real->caps.contains("format=(string)I420") && real->caps.contains("width=(int)160") && real->caps.contains("height=(int)120"),
                 qPrintable(real->caps));
        QCOMPARE(c->view(mix).sinks.size(), 1);

        // second source by "Add source", linked to the same compositor with "Request a pad"
        QString b = addSource("videotestsrc", [&](PropertyPage* d) {
            setNumber(d, "num-buffers", 30);
            setChoice(d, "pattern", "smpte");
        });
        QVERIFY(!b.isEmpty());
        clickPad(b, "src", true);
        auto* picker = shown<PickerPage>();
        QVERIFY(picker);
        auto* list = picker->findChild<QListWidget*>("pickerList");
        QCOMPARE(list->item(0)->text(), QString("Elements in the pipeline"));
        QCOMPARE(list->item(1)->text(), mix + " · sink_%u");
        QVERIFY(shotOf(w->drawer(), "08_picker_request_a_pad"));
        pickRow(picker, mix + " · sink_%u", "Request a pad");
        auto* caps = capsPage();
        QVERIFY(caps);
        QCOMPARE(c->plan().links.size(), size_t(2));  // the link exists before the dialog's answer
        QVERIFY(caps->findChild<QWidget*>("padSettings")->isVisible());
        selectMedia(caps, "video/x-raw");
        pinField(caps, "format", "I420");
        pinField(caps, "width", "320");
        pinField(caps, "height", "240");
        caps->findChild<QWidget*>("padEditor_xpos")->findChild<QSpinBox*>()->setValue(160);
        QVERIFY(shotOf(w->bottomTabs(), "09_caps_page_pad_settings"));
        timed("Link to a Request pad: apply caps + xpos", [&] { press(caps, "saveButton"); });
        idle();
        QVERIFY2(c->result().ok(), qPrintable(c->result().status + ": " + c->result().message));
        QCOMPARE(c->plan().links[1].sinkPadProps.at("xpos"), std::string("160"));
        QCOMPARE(text(c->plan().links[1].sinkPad), QString("sink_%u"));
        QCOMPARE(c->view(mix).sinks.size(), 2);
        QCOMPARE(c->view(mix).sinks[1].state, PadView::Linked);
        QVERIFY(c->view(mix).sinks[0].label.startsWith("sink_"));
        QVERIFY(c->view(mix).sinks[1].label.startsWith("sink_"));
        QVERIFY(c->view(mix).sinks[0].label != c->view(mix).sinks[1].label);
        // The pad behind link k must be the pad the GUI shows in row k (label, caps tooltip). The tool reports the Request
        // sink pads of one template in reverse order (newest first), so the rows are swapped: a TOOL deviation (SPEC 3:
        // "depends on the template and the order"), reported to the engine owner. XPASS here means the tool was fixed.
        bool inOrder = c->view(mix).sinks[0].tip.contains("width=(int)160") && c->view(mix).sinks[1].tip.contains("width=(int)320");
        qInfo() << "compositor sink rows:" << c->view(mix).sinks[0].label << c->view(mix).sinks[0].tip.left(70) << "|" << c->view(mix).sinks[1].label << c->view(mix).sinks[1].tip.left(70);
        if (!inOrder) QEXPECT_FAIL("", "gps-update lists Request sink pads newest first (tool deviates from SPEC 3)", Continue);
        QVERIFY(inOrder);
        QVERIFY(w->scene()->links()[1]->view().badge.startsWith("caps"));
        qInfo() << "compositor output caps with xpos=160:" << c->result().pad(mix, "src", true)->caps;

        QString conv = addBehind(mix, "src", "videoconvert");
        QString sink = addBehind(conv, "src", "fakesink");
        QVERIFY(!sink.isEmpty());
        QVERIFY2(c->result().isComplete(), qPrintable(c->result().status + ": " + c->result().message));
        QVERIFY(shot("10_compositor_two_branches"));

        // the pad setting stays editable: Edit caps shows xpos=160 again
        linkMenu(b, "src", "actionEditCaps");
        caps = capsPage();
        QVERIFY(caps);
        QCOMPARE(caps->findChild<QWidget*>("padEditor_xpos")->findChild<QSpinBox*>()->value(), 160);
        QCOMPARE(caps->findChild<QComboBox*>("mediaCombo")->currentText(), QString("video/x-raw"));
        press(caps, "cancelButton");
        QTest::qWait(50);

        // run it for real (saved through the window)
        QVERIFY(w->saveFile(work.filePath("mix.conf")));
        QSignalSpy ended(c.get(), &Controller::runFinished);
        w->findChild<QAction*>("actionRun")->trigger();
        QVERIFY(waitFor([&] { return ended.count() == 1; }, 30000));
        QCOMPARE(ended[0][0].toString(), QString("eos"));
    }

    // =================================================================================================================
    // 5. cut / replace / delete source (Free-Canvas: no cascade deletion!)
    // =================================================================================================================

    void cutAndReplaceAskWithTheRealLists() {
        QString a = addSource("videotestsrc", [&](PropertyPage* d) { setNumber(d, "num-buffers", 10); });
        QString b = addBehind(a, "src", "videoconvert");
        QString cc = addBehind(b, "src", "videoscale");
        QString d = addBehind(cc, "src", "fakesink");
        QVERIFY(!d.isEmpty());
        QVERIFY2(c->result().isComplete(), qPrintable(c->result().message));

        // Cut link on b.src: only the link b -> cc is removed; cc and d remain on the free canvas as untracked!
        timed("Cut link (no cascade delete)", [&] { linkMenu(b, "src", "actionCut"); });
        idle();
        QCOMPARE(c->plan().elements.size(), size_t(4));
        QCOMPARE(c->plan().links.size(), size_t(2));  // a -> b and cc -> d remain
        QVERIFY2(c->result().ok(), qPrintable(c->result().message));
        QCOMPARE(w->scene()->pad(b, "src", true)->view().state, PadView::Free);
        QVERIFY(!c->result().isComplete());
        QVERIFY(!c->canRun());
        QVERIFY(c->canSave());
        QVERIFY(shot("11_confirm_3_elements"));

        // Re-link b.src -> cc.sink, then replace cc (behind b.src) with videoflip: only cc is replaced, d stays on canvas!
        linkTo(b, "src", cc + " · sink");
        QVERIFY2(c->result().isComplete(), qPrintable(c->result().message));
        linkMenu(b, "src", "actionReplace");
        auto* picker = shown<PickerPage>();
        QVERIFY(picker);
        QVERIFY(w->drawer()->title().startsWith("Replace after"));
        pickRow(picker, "videoflip");
        auto* props = shown<PropertyPage>();
        QVERIFY(props);
        QVERIFY(w->flows()->propertiesTitle().startsWith("Replace after"));
        setChoice(props, "method", "vertical-flip");
        timed("Replace element (in place)", [&] { press(props, "saveButton"); });
        idle();
        if (auto* caps = capsPage()) {
            timed("Apply caps after replace", [&] { press(caps, "saveButton"); });
            idle();
        }
        QVERIFY2(c->result().ok(), qPrintable(c->result().message));
        QVERIFY(c->plan().find(cc.toStdString()) == nullptr);
        QVERIFY(c->plan().find(d.toStdString()) != nullptr);  // d was NOT cascade-deleted!
        const gps::PlanElement* flip = c->plan().find(c->plan().elements.back().name);
        QCOMPARE(text(flip->factory), QString("videoflip"));
        QCOMPARE(flip->props.at("method"), std::string("vertical-flip"));
        QCOMPARE(c->plan().linkFrom(b.toStdString(), "src") >= 0, true);
        QVERIFY(shot("12_element_replaced"));
    }

    void deleteSourceCascades() {
        QString a = addSource("videotestsrc", [&](PropertyPage* d) { setNumber(d, "num-buffers", 10); });
        QString convA = addBehind(a, "src", "videoconvert");
        QString mix = addBehind(convA, "src", "compositor");
        QString bb = addSource("videotestsrc", [&](PropertyPage* d) { setNumber(d, "num-buffers", 10); });
        linkTo(bb, "src", mix + " · sink_%u");
        QString sink = addBehind(mix, "src", "fakesink");
        QVERIFY(!sink.isEmpty());
        QVERIFY2(c->result().isComplete(), qPrintable(c->result().status + ": " + c->result().message));
        QCOMPARE(c->view(mix).sinks.size(), 2);

        // Delete source a: ONLY source a is deleted; convA stays on the free canvas as untracked!
        rightClickElement(a);
        timed("Delete source (no cascade delete)", [&] { choose("actionDelete"); });
        idle();
        QVERIFY(!c->plan().find(a.toStdString()));
        QVERIFY(c->plan().find(convA.toStdString()));  // convA stays on the free canvas!
        QCOMPARE(c->plan().elements.size(), size_t(4));  // convA (untracked), bb, mix, sink
        // Cut convA -> mix so mix has only bb's tracked input and is complete
        timed("Cut untracked convA -> mix link", [&] { linkMenu(convA, "src", "actionCut"); });
        idle();
        QVERIFY2(c->result().isComplete(), qPrintable(c->result().status + ": " + c->result().message));
        QCOMPARE(c->view(mix).sinks.size(), 1);
        QCOMPARE(c->view(mix).sinks[0].state, PadView::Linked);
        QVERIFY(shot("13_delete_source_chain"));
    }

    // =================================================================================================================
    // 6. failed update keeps the change, disables Save; the fix brings it back
    // =================================================================================================================

    void failedUpdateKeepsTheChangeAndDisablesSave() {
        QString file = addSource("filesrc", [&](PropertyPage* d) { setText(d, "location", "/does/not/exist.avi"); });
        QVERIFY(!file.isEmpty());
        QVERIFY(c->plan().find(file.toStdString()));  // the change stayed
        QCOMPARE(c->result().status, QString("error"));
        QCOMPARE(c->result().errorElement, file);
        QVERIFY2(c->result().message.contains("exist.avi") || c->result().message.contains("not found"), qPrintable(c->result().message));
        QVERIFY(!c->canSave());
        QVERIFY(!w->findChild<QAction*>("actionSave")->isEnabled());
        QVERIFY(!w->findChild<QAction*>("actionRun")->isEnabled());
        QVERIFY(!w->saveFile(work.filePath("must-not-exist.conf")));
        QVERIFY(!QFile::exists(work.filePath("must-not-exist.conf")));
        QVERIFY(w->scene()->element(file)->view().isError);
        QCOMPARE(w->scene()->element(file)->view().note, c->result().message);
        QVERIFY(w->updatePanel()->findChild<QLabel*>("updateStatus")->text().startsWith("Problem"));
        QVERIFY(w->findChild<QLabel*>("stateLabel")->text().contains("reported a problem"));
        QVERIFY(w->windowTitle().endsWith("*"));
        QVERIFY(shot("14_failed_validation"));

        // add an element behind the failed one? The update failed before the pad was known; the user fixes the property
        doubleClickElement(file);
        auto* dialog = shown<PropertyPage>();
        QVERIFY(dialog);
        QCOMPARE(in<QLineEdit>(dialog, "editor_location")->text(), QString("/does/not/exist.avi"));
        setText(dialog, "location", avi);
        timed("Edit properties: fix the location", [&] { press(dialog, "saveButton"); });
        idle();
        QCOMPARE(c->result().status, QString("ok"));
        QVERIFY(c->canSave());
        QVERIFY(w->findChild<QAction*>("actionSave")->isEnabled());
        QVERIFY(!w->scene()->element(file)->view().isError);
        QVERIFY(w->saveFile(work.filePath("fixed.conf")));
    }

    void refusedLinkIsReportedAndNothingBreaks() {
        // raw video cannot enter jpegdec: GStreamer refuses the link
        c->setUpdateTimeoutMs(3000);
        QString a = addSource("videotestsrc", [&](PropertyPage* d) { setNumber(d, "num-buffers", 5); });
        clickPad(a, "src", true);
        auto* picker = shown<PickerPage>();
        QVERIFY(picker);
        pickRow(picker, "jpegdec");  // not in the compatible list, but the user may still choose it
        auto* props = shown<PropertyPage>();
        QVERIFY(props);
        QElapsedTimer clock;
        clock.start();
        timed("Refused link (jpegdec <- raw video)", [&] { press(props, "saveButton"); });
        idle();
        qInfo() << "refused link: status" << c->result().status << "message" << c->result().message << "refused" << c->result().refused
                << "after" << clock.elapsed() << "ms";
        QVERIFY(c->result().status == "ok" || c->result().status == "timeout" || c->result().status == "error");
        // whichever status the tool answers, the GUI must not offer Run
        QVERIFY(!c->canRun());
        if (c->result().status == "ok") QVERIFY(!c->result().refused.isEmpty());
        QVERIFY(c->plan().elements.size() == 2);  // the change stayed
        QVERIFY(shot("15_refused_link"));
    }

    // =================================================================================================================
    // 7. exposed parameter, caps choice on a file output, Run through the GUI, gps-run with a parameter
    // =================================================================================================================

    void exposedParameterAndRunFromTheSavedConf() {
        QString out = work.filePath("out.raw");
        QFile::remove(out);
        QString src = addSource("videotestsrc", [&](PropertyPage* d) {
            setNumber(d, "num-buffers", 5);
            expose(d, "num-buffers");
        });
        QVERIFY(!src.isEmpty());
        QCOMPARE(c->plan().exposed.size(), size_t(1));
        QVERIFY(c->view(src).propLines.join("\n").contains("↗ num-buffers = 5"));
        QString sink = addBehind(src, "src", "filesink", [&](PropertyPage* d) { setText(d, "location", out); }, [&](CapsPage* d) {
            selectMedia(d, "video/x-raw");
            pinField(d, "format", "I420");
            pinField(d, "width", "160");
            pinField(d, "height", "120");
        });
        QVERIFY(!sink.isEmpty());
        QVERIFY2(c->result().isComplete(), qPrintable(c->result().status + ": " + c->result().message));
        QVERIFY(c->result().pad(src, "src", true)->caps.contains("format=(string)I420"));

        QString conf = work.filePath("expose.conf");
        QVERIFY(w->saveFile(conf));
        QString launch = w->findChild<QLabel*>("commandLabel")->text();
        QVERIFY2(launch.contains("gst-launch-1.0 expose") && launch.contains("num-buffers=VALUE"), qPrintable(launch));
        QFile f(conf);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QString saved = QString::fromUtf8(f.readAll());
        QVERIFY(saved.contains("[expose:num-buffers]"));
        QVERIFY(saved.contains("complete=1"));
        QVERIFY(saved.contains("name=expose"));

        QSignalSpy ended(c.get(), &Controller::runFinished);
        QElapsedTimer clock;
        clock.start();
        w->findChild<QAction*>("actionRun")->trigger();
        QVERIFY(waitFor([&] { return ended.count() == 1; }, 30000));
        g_timings.append({"Run: videotestsrc ! filesink (5 frames, I420 160x120)", double(clock.elapsed()), 0});
        QCOMPARE(ended[0][0].toString(), QString("eos"));
        QCOMPARE(QFileInfo(out).size(), qint64(5 * 160 * 120 * 3 / 2));  // 5 raw I420 frames, so the caps choice reached the real pipeline
        QVERIFY(shot("16_ran"));

        // the exposed name reaches the pipeline from the command line
        QFile::remove(out);
        QProcess run;
        run.start(QString::fromUtf8(goodBin) + "/gps-run", {conf, "num-buffers=2"});
        QVERIFY(run.waitForFinished(30000));
        QCOMPARE(run.exitCode(), 0);
        QVERIFY2(run.readAllStandardOutput().contains("\"status\":\"eos\""), "gps-run");
        QCOMPARE(QFileInfo(out).size(), qint64(2 * 160 * 120 * 3 / 2));
        QProcess bad;
        bad.start(QString::fromUtf8(goodBin) + "/gps-run", {conf, "nonsense=1"});
        QVERIFY(bad.waitForFinished(30000));
        QCOMPARE(bad.exitCode(), 2);
    }

    // =================================================================================================================
    // 8. save / open round trip with Sometimes pads and Request pads
    // =================================================================================================================

    void saveAndOpenRoundTrip() {
        AviPipeline p = buildAvi(true);
        QVERIFY(!p.asink.isEmpty());
        QVERIFY(c->result().isComplete());
        QString conf = work.filePath("roundtrip.conf");
        QVERIFY(w->saveFile(conf));
        QString links = linkText();
        QString planText = text(c->plan().toConf());
        QStringList names;
        for (const gps::PlanElement& e : c->plan().elements) names << text(e.name);

        // New: empty canvas
        c->newPlan();
        idle();
        QVERIFY(c->plan().empty());
        QCOMPARE(w->scene()->items().size(), 0);

        // Open: the plan returns; the update brings the Sometimes pads back and the pipeline is complete again
        QElapsedTimer clock;
        clock.start();
        w->openFile(conf);
        QVERIFY(waitFor([&] { return !c->busy() && !c->plan().empty() && c->result().valid(); }, 30000));
        idle();
        g_timings.append({"Open: 6 element avi pipeline", double(clock.elapsed()), c->result().ms});
        qInfo() << "TIMING Open (avi pipeline):" << clock.elapsed() << "ms";
        QCOMPARE(linkText(), links);
        QStringList again;
        for (const gps::PlanElement& e : c->plan().elements) again << text(e.name);
        QCOMPARE(again, names);
        QVERIFY2(c->result().isComplete(), qPrintable(c->result().status + ": " + c->result().message));
        QVERIFY(c->canRun());
        QCOMPARE(w->scene()->links().size(), int(c->plan().links.size()));
        QVERIFY(!c->dirty());
        QVERIFY(w->windowTitle().contains("roundtrip.conf"));
        QVERIFY(shot("17_opened_file"));

        // saving the opened plan again writes the same text (positions, names, links)
        QVERIFY(w->saveFile(work.filePath("roundtrip2.conf")));
        QFile a(conf), b(work.filePath("roundtrip2.conf"));
        QVERIFY(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
        QString first = QString::fromUtf8(a.readAll()), second = QString::fromUtf8(b.readAll());
        first.replace(QRegularExpression("name=.*\n"), "");
        second.replace(QRegularExpression("name=.*\n"), "");
        QCOMPARE(second, first);
        QVERIFY(planText.contains("[link:4]"));
    }

    // =================================================================================================================
    // 9. helpers that crash, hang, or talk nonsense: the GUI survives and recovers
    // =================================================================================================================

    void aCrashingUpdateHelperIsSurvived() {
        QString src = addSource("videotestsrc");
        QVERIFY(!src.isEmpty());
        QVERIFY(need(*c, "videoconvert"));
        QString dir = helperDirWith("gps-update", "#!/bin/sh\nkill -SEGV $$\n");
        QVERIFY(!dir.isEmpty());
        qputenv("GPS_BIN_DIR", dir.toUtf8());

        QVERIFY(need(*c, "videoconvert"));
        QVERIFY(c->addAfter(src, "src", "videoconvert", "sink", {}));
        QVERIFY(waitFor([&] { return !c->busy(); }, 15000));
        QString conv = text(c->plan().elements.back().name);
        QCOMPARE(c->result().status, QString("crashed"));
        QVERIFY2(c->result().message.contains(conv), qPrintable(c->result().message));
        QVERIFY(c->result().message.contains("stop unexpectedly"));
        QVERIFY(!c->canSave());
        idle();
        QVERIFY(w->scene()->element(conv)->view().isError);
        QVERIFY(w->updatePanel()->findChild<QLabel*>("updateStatus")->text().contains("stopped unexpectedly"));
        QVERIFY(w->isVisible());
        QVERIFY(shot("18_helper_crashed"));

        // alive and well: the real helper comes back and Validate (F5) heals the state
        qputenv("GPS_BIN_DIR", goodBin);
        QVERIFY(w->findChild<QAction*>("actionUpdate")->isEnabled());
        timed("Validate (F5) after the crash", [&] { w->findChild<QAction*>("actionUpdate")->trigger(); });
        idle();
        QCOMPARE(c->result().status, QString("ok"));
        QVERIFY(c->canSave());
        QVERIFY(!w->scene()->element(conv)->view().isError);
    }

    void aHangingUpdateHelperIsKilledAfterTheTimeout() {
        QString src = addSource("videotestsrc");
        QVERIFY(!src.isEmpty());
        QString dir = helperDirWith("gps-update", "#!/bin/sh\nexec sleep 1000\n");
        qputenv("GPS_BIN_DIR", dir.toUtf8());
        c->setUpdateTimeoutMs(500);
        QVERIFY(need(*c, "videoconvert"));
        QElapsedTimer clock;
        clock.start();
        QVERIFY(c->addAfter(src, "src", "videoconvert", "sink", {}));
        QVERIFY(c->busy());
        // while the helper hangs, the GUI is still alive: it paints, and the actions are off
        QVERIFY(!w->findChild<QAction*>("actionSave")->isEnabled());
        QVERIFY(!w->findChild<QAction*>("actionAddSource")->isEnabled());
        QVERIFY(w->findChild<QLabel*>("stateLabel")->text().contains("locked while validating"));
        QVERIFY(shot("19_validating_hung"));
        QVERIFY(waitFor([&] { return !c->busy(); }, 20000));
        qint64 took = clock.elapsed();
        qInfo() << "hanging gps-update killed after" << took << "ms (timeout 500 + 2000)";
        QVERIFY2(took >= 2400 && took < 9000, qPrintable(QString::number(took)));
        QCOMPARE(c->result().status, QString("hung"));
        QVERIFY(c->result().message.contains("did not answer in time"));
        QVERIFY(!c->canSave());
        QVERIFY(w->isVisible());
        idle();
        QVERIFY(w->findChild<QAction*>("actionAddSource")->isEnabled());
        QVERIFY(shot("20_after_the_hung_helper_was_killed"));
    }

    void garbageAndCrashingRunHelpersAreSurvived() {
        QString src = addSource("videotestsrc", [&](PropertyPage* d) { setNumber(d, "num-buffers", 3); });
        QString sink = addBehind(src, "src", "fakesink");
        QVERIFY(!sink.isEmpty());
        QVERIFY(c->canRun());

        // gps-run dies by SIGSEGV
        qputenv("GPS_BIN_DIR", helperDirWith("gps-run", "#!/bin/sh\nkill -SEGV $$\n").toUtf8());
        QSignalSpy ended(c.get(), &Controller::runFinished);
        QVERIFY(c->run());
        QVERIFY(waitFor([&] { return ended.count() == 1; }, 15000));
        QCOMPARE(ended[0][0].toString(), QString("crashed"));
        QVERIFY(!c->running());
        QVERIFY(w->isVisible());

        // gps-update prints no JSON
        qputenv("GPS_BIN_DIR", helperDirWith("gps-update", "#!/bin/sh\necho 'this is not json'\nexit 0\n").toUtf8());
        QVERIFY(c->refresh());
        QVERIFY(waitFor([&] { return !c->busy(); }, 15000));
        QCOMPARE(c->result().status, QString("stopped"));
        QVERIFY(c->result().message.contains("stopped unexpectedly"));
        QVERIFY(!c->canSave());

        // the real tools again: everything works
        qputenv("GPS_BIN_DIR", goodBin);
        QVERIFY(c->refresh());
        QVERIFY(waitFor([&] { return !c->busy(); }, 15000));
        QVERIFY(c->canRun());
        ended.clear();
        QVERIFY(c->run());
        QVERIFY(waitFor([&] { return ended.count() == 1; }, 30000));
        QCOMPARE(ended[0][0].toString(), QString("eos"));
    }

    // =================================================================================================================
    // 10. What the screenshots of the real data showed (each of these failed before the fix, see ajan_integration.md)
    // =================================================================================================================

    // The "caps · N" chip sits on a 40 px link: it must not hide the pad circles at either end.
    void aCapsChipLeavesTheBothPadCirclesVisible() {
        QString src = addSource("videotestsrc", [&](PropertyPage* d) { setNumber(d, "num-buffers", 5); });
        QString sink = addBehind(src, "src", "fakesink", {}, [&](CapsPage* d) {
            selectMedia(d, "video/x-raw");
            pinField(d, "width", "160");
            pinField(d, "height", "120");
        });
        QVERIFY(!sink.isEmpty());
        QCOMPARE(w->scene()->links().size(), 1);
        QVERIFY(!w->scene()->links()[0]->view().badge.isEmpty());
        QRectF area;
        QImage image = renderScene(&area);
        QVERIFY2(padIsDrawn(image, area, src, "src", true, true), "the chip covers the source pad circle");
        QVERIFY2(padIsDrawn(image, area, sink, "sink", false, true), "the chip covers the sink pad circle");
        QVERIFY(shot("21_caps_chip_pad_circles_visible"));
    }

    // GStreamer error texts are long. Up to three lines must be drawn inside the box; more must end in "…" (tooltip: all of it).
    void aLongErrorTextIsFullyDrawnInsideTheBox() {
        for (int pathLength : {25, 45, 400}) {
            QString path = "/does/not/exist/" + QString("d").repeated(pathLength) + ".avi";
            c->newPlan();
            QString file = addSource("filesrc", [&](PropertyPage* d) { setText(d, "location", path); });
            QVERIFY(!file.isEmpty());
            QCOMPARE(c->result().status, QString("error"));
            const ElementView view = c->view(file);
            QVERIFY(view.isError);
            // a path without spaces in the message must not stretch the window (the Validation label cannot wrap it)
            QVERIFY2(w->width() <= 1400, qPrintable(QString("window grew to %1 px").arg(w->width())));
            QRectF area;
            QImage image = renderScene(&area);
            ElementItem* item = w->scene()->element(file);
            QRectF box(item->pos() - area.topLeft(), QSizeF(metrics::kBoxWidth, view.height()));
            int firstInk = -1, lastInk = -1;
            for (int y = int(box.top()) + int(metrics::kHeader) + 30; y < int(box.bottom()) - 1; ++y) {
                for (int x = int(box.left()) + 12; x < int(box.right()) - 12; ++x) {
                    QColor px = image.pixelColor(x, y);
                    if (px.red() > 150 && px.green() < 140 && px.blue() < 140) {  // the error colour, anti-aliased
                        if (firstInk < 0) firstInk = y;
                        lastInk = y;
                        break;
                    }
                }
            }
            QFont small;
            small.setPointSizeF(8);
            QFontMetrics fm(small);
            int lines = fm.boundingRect(QRect(0, 0, int(metrics::kBoxWidth) - 24, 10000), Qt::TextWordWrap | Qt::TextWrapAnywhere, view.note).height() / fm.lineSpacing();
            qInfo() << "path" << pathLength << "chars: the text needs" << lines << "lines; ink rows" << firstInk << "-" << lastInk << "box bottom" << box.bottom();
            QVERIFY2(firstInk > 0, "no error text drawn");
            // the ink spans min(lines, 3) lines of 16 px: the last row is at least that far below the first, and inside the box
            int shown = qMin(lines, 3);
            QVERIFY2(lastInk - firstInk >= (shown - 1) * int(metrics::kPropLine), qPrintable(QString("only %1 px of text for %2 lines").arg(lastInk - firstInk).arg(shown)));
            QVERIFY2(lastInk < box.bottom() - 3, "the error text touches the box border");
            // nothing may be drawn to the right of the box (an unbreakable path must wrap inside it)
            int outside = 0;
            for (int y = int(box.top()); y < int(box.bottom()); ++y) {
                for (int x = int(box.right()) + 3; x < qMin(image.width(), int(box.right()) + 40); ++x) {
                    QColor px = image.pixelColor(x, y);
                    outside += px.red() > 150 && px.green() < 140 && px.blue() < 140;
                }
            }
            QCOMPARE(outside, 0);
            QVERIFY2(item->toolTip().contains(view.note), "the tooltip must hold the whole text");
            if (lines > 3) QVERIFY(ElementItem::fitNote(view.note, small).endsWith("…"));
            else QCOMPARE(ElementItem::fitNote(view.note, small), view.note);
            QVERIFY(shot(QString("22_long_error_text_%1").arg(pathLength)));
        }
    }

    // Pad rows show the pad name and the media type. The media types of real pads must be readable, not cut to "video/x-ms…".
    void padMediaTypesAreNotCutOff() {
        AviPipeline p = buildAvi(true);
        QVERIFY(!p.asink.isEmpty());
        int checked = 0;
        for (const gps::PlanElement& e : c->plan().elements) {
            ElementView v = c->view(text(e.name));
            for (const QList<PadView>& pads : {v.sinks, v.srcs}) {
                for (const PadView& pad : pads) {
                    if (pad.media.isEmpty()) continue;
                    ElementItem::PadLabel label = ElementItem::padLabel(pad);
                    QVERIFY2(label.media == pad.media, qPrintable(QString("%1.%2: shows \"%3\" for \"%4\"").arg(v.name, pad.label, label.media, pad.media)));
                    ++checked;
                }
            }
        }
        QVERIFY2(checked >= 8, qPrintable(QString::number(checked)));
    }

    // With pad settings on screen (compositor), the caps fields must keep a usable area.
    void capsPageKeepsRoomForTheFieldsWhenThePadHasSettings() {
        QString a = addSource("videotestsrc");
        QVERIFY(!a.isEmpty());
        QString mix;
        {
            clickPad(a, "src", true);
            auto* picker = shown<PickerPage>();
            QVERIFY(picker);
            pickRow(picker, "compositor");
            auto* props = shown<PropertyPage>();
            QVERIFY(props);
            press(props, "saveButton");
            idle();
            mix = text(c->plan().elements.back().name);
        }
        auto* caps = capsPage();
        QVERIFY(caps);
        selectMedia(caps, "video/x-raw");
        QTest::qWait(100);
        auto* pages = caps->findChild<QStackedWidget*>();
        QVERIFY(pages);
        qInfo() << "caps page size" << caps->size() << "field area" << pages->size();
        // width, height, format, framerate, ... one row is about 36 px: four rows must be visible
        QVERIFY2(pages->height() >= 4 * 36, qPrintable(QString("field area is only %1 px high").arg(pages->height())));
        press(caps, "cancelButton");
    }

    // Insert queue on a link between two boxes that are 280 px apart: the queue takes the place between them, left to right.
    void insertedQueueKeepsTheFlowLeftToRight() {
        QString src = addSource("videotestsrc", [&](PropertyPage* d) { setNumber(d, "num-buffers", 5); });
        QString a = addBehind(src, "src", "videoconvert");
        QString sink = addBehind(a, "src", "fakesink");
        QVERIFY(!sink.isEmpty());
        timed("Insert queue (layout)", [&] { linkMenu(a, "src", "actionInsertQueue"); });
        idle();  // checkLayout() runs here: no overlap, no backwards link
        QString queue = text(c->plan().elements.back().name);
        const gps::PlanElement* q = c->plan().find(queue.toStdString());
        QVERIFY(q->x > c->plan().find(a.toStdString())->x);
        QVERIFY(q->x < c->plan().find(sink.toStdString())->x);
        QVERIFY(shot("23_queue_layout"));
    }
};

QTEST_MAIN(IntegrationTest)
#include "integration_test.moc"
