// ux_test.cpp – the behaviour added by UI_RULES: validation indicator, slow choices, cancel, helper crash message,
// support URL, first-run states, spinner, theme contrast, settings, English everywhere. Offscreen, fake helpers.
#include <QAbstractButton>
#include <QAction>
#include <QClipboard>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTabBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <array>
#include <csignal>
#include <cerrno>

#include "canvasscene.h"
#include "canvasview.h"
#include "capspage.h"
#include "confirmdialog.h"
#include "drawer.h"
#include "elementitem.h"
#include "flows.h"
#include "logpanel.h"
#include "mainwindow.h"
#include "paditem.h"
#include "pickerpage.h"
#include "propertypage.h"
#include "setuppage.h"
#include "updatepanel.h"
#include "spinner.h"
#include "supportreport.h"
#include "testsupport.h"
#include "theme.h"
#include "validationindicator.h"

using namespace support;

namespace {

struct Capture : UrlOpener {
    QList<QUrl> urls;
    bool works = true;  // false: there is no web browser
    bool open(const QUrl& url) override {
        urls << url;
        return works;
    }
};

// Every text a user can see in this widget tree.
QStringList strings(QWidget* root) {
    QStringList out;
    auto add = [&out](const QString& s) {
        if (!s.trimmed().isEmpty()) out << s;
    };
    QList<QWidget*> all = root->findChildren<QWidget*>();
    all << root;
    for (QWidget* w : all) {
        add(w->windowTitle());
        add(w->toolTip());
        if (auto* l = qobject_cast<QLabel*>(w)) add(l->text());
        if (auto* b = qobject_cast<QAbstractButton*>(w)) add(b->text());
        if (auto* e = qobject_cast<QLineEdit*>(w)) add(e->placeholderText()), add(e->text());
        if (auto* g = qobject_cast<QGroupBox*>(w)) add(g->title());
        if (auto* t = qobject_cast<QTabBar*>(w)) {
            for (int i = 0; i < t->count(); ++i) add(t->tabText(i)), add(t->tabToolTip(i));
        }
        if (auto* c = qobject_cast<QComboBox*>(w)) {
            for (int i = 0; i < c->count(); ++i) add(c->itemText(i));
        }
        if (auto* l = qobject_cast<QListWidget*>(w)) {
            for (int i = 0; i < l->count(); ++i) add(l->item(i)->text()), add(l->item(i)->data(Qt::UserRole + 3).toString());
        }
        if (auto* t = qobject_cast<QTableWidget*>(w)) {
            for (int c = 0; c < t->columnCount(); ++c) add(t->horizontalHeaderItem(c) ? t->horizontalHeaderItem(c)->text() : QString());
            for (int r = 0; r < t->rowCount(); ++r) {
                for (int c = 0; c < t->columnCount(); ++c) add(t->item(r, c) ? t->item(r, c)->text() : QString());
            }
        }
        if (auto* s = qobject_cast<QStatusBar*>(w)) add(s->currentMessage());
        if (auto* l = qobject_cast<QPlainTextEdit*>(w)) add(l->toPlainText());
    }
    for (QAction* a : root->findChildren<QAction*>()) {
        add(a->text());
        add(a->toolTip());
        add(a->statusTip());
    }
    return out;
}

bool pidAlive(qint64 pid) { return pid > 0 && (::kill(static_cast<pid_t>(pid), 0) == 0 || errno != ESRCH); }

qint64 readPid(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll().trimmed().toLongLong() : 0;
}

}  // namespace

class UxTest : public QObject {
    Q_OBJECT

    std::unique_ptr<Controller> c;
    QTemporaryDir work;
    Capture capture;

    StartupResult check() {
        StartupCheck startup;
        QSignalSpy done(&startup, &StartupCheck::finished);
        startup.start();
        if (!waitFor([&] { return done.count() == 1; })) return StartupResult();
        return startup.result();
    }

    static void env(const char* version, const char* noGst, const char* broken) {
        qputenv("GPS_FAKE_GST_VERSION", version);
        qputenv("GPS_FAKE_NO_GST", noGst);
        qputenv("GPS_FAKE_BROKEN", broken);
    }

    // Counts pixels close to the accent colour.
    static int accentPixels(const QImage& image) {
        int n = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                QColor p = image.pixelColor(x, y);
                n += qAbs(p.red() - Theme::accent().red()) < 30 && qAbs(p.green() - Theme::accent().green()) < 30 && qAbs(p.blue() - Theme::accent().blue()) < 30;
            }
        }
        return n;
    }

private slots:
    void initTestCase() {
        isolateSettings();
        Theme::apply(*qApp);
        QVERIFY(work.isValid());
    }
    void init() {
        env("1.20.3", "", "");
        qunsetenv("GPS_FAKE_PIDFILE");
        qunsetenv("GPS_FAKE_IGNORE_TERM");
        qunsetenv("GPS_FAKE_SLOW_MS");
        c = makeController();
        c->setUrlOpener(&capture);
        capture.urls.clear();
        capture.works = true;
    }
    void cleanup() {
        env("1.20.3", "", "");
        c.reset();
    }

    // ---- a person without a web browser (a server, a container) still gets the report

    void withoutABrowserTheReportGoesToTheClipboardAndTheUserIsTold() {
        capture.works = false;
        QGuiApplication::clipboard()->setText("before");
        QSignalSpy notice(c.get(), &Controller::notice);
        c->requestSupport("Something went wrong");
        QCOMPARE(capture.urls.size(), 1);  // it did try the browser
        QCOMPARE(notice.size(), 1);
        QVERIFY(notice.at(0).at(0).toString().contains("copied to the clipboard"));
        QVERIFY(notice.at(0).at(0).toString().contains("github.com/mevlt01001/GstPipeStudio/issues/new"));
        QString copied = QGuiApplication::clipboard()->text();
        QVERIFY(copied.startsWith("Something went wrong"));
        QVERIFY(copied.contains("**Details**"));
        // With a browser nothing is copied and nobody is told.
        capture.works = true;
        QGuiApplication::clipboard()->setText("before");
        c->requestSupport("Again");
        QCOMPARE(QGuiApplication::clipboard()->text(), QString("before"));
        QCOMPARE(notice.size(), 1);
    }

    // ---- 1. English everywhere

    void noVisibleStringHasATurkishLetterOrWord() {
        // The Turkish letters are written as code points so that this file is clean for a grep too.
        static const QRegularExpression turkish(
            "[\\x{e7}\\x{11f}\\x{131}\\x{f6}\\x{15f}\\x{fc}\\x{c7}\\x{11e}\\x{130}\\x{d6}\\x{15e}\\x{dc}]"
            "|\\b(Kaydet|Iptal|Guncelle|Calistir|Kaynak|Gunluk|Otomatik|Sabitle|Vazgec|Tamam|Hata|Baglanti|Eleman|Dosya|Sil|Sec|Ekle|Yeni|Ac)\\b");
        MainWindow w(c.get());
        w.show();
        auto verify = [&](QWidget* root, const QString& where) {
            for (const QString& s : strings(root)) QVERIFY2(!turkish.match(s).hasMatch(), qPrintable(where + ": " + s));
            QVERIFY2(strings(root).size() > 3, qPrintable(where + " has no strings"));
        };
        QVERIFY(waitFor([&] { return !w.findChild<QLabel*>("environmentLabel")->text().isEmpty(); }));
        verify(&w, "empty window");
        for (QAction* a : w.findChild<QMenuBar*>("applicationMenu")->actions()) {
            QVERIFY(a->menu());
            for (QAction* item : a->menu()->actions()) QVERIFY2(!turkish.match(item->text() + item->toolTip()).hasMatch(), qPrintable(item->text()));
        }

        // a finished pipeline, a failure and the log
        QString src = addSource(*c, "videotestsrc");
        QString sink = addAfter(*c, src, "src", "fakesink", "sink", withProps({{"fake-error", "problem text"}}));
        verify(&w, "failed validation");
        QVERIFY(c->editElement(sink, {}));
        QVERIFY(settle(*c));
        verify(&w, "finished pipeline");

        // every drawer page and bottom tab page
        w.flows()->chooseSource();
        verify(w.drawer(), "add source");
        w.flows()->fromFreePad(src, "src");
        QVERIFY(pageOf(w, "pickerPage"));
        verify(w.drawer(), "picker after a pad");
        w.flows()->editProperties(src);
        QVERIFY(pageOf(w, "propertyPage"));
        verify(w.propertiesTab(), "properties");
        w.flows()->editCaps(src, "src");
        QVERIFY(pageOf(w, "capsPage"));
        verify(w.capsTab(), "caps");
        w.flows()->closeProperties();
        w.flows()->closeCaps();
        w.drawer()->dismiss();
        ConfirmDialog confirm("Remove elements", "These 2 elements will be removed, with 1 link", {"a (x)"}, "Delete 2 elements");
        verify(&confirm, "confirmation");

        // the slow, cancelling and stopped states of the indicator
        qint64 now = 0;
        c->slowWatch()->setClock([&now] { return now; });
        QVERIFY(c->editElement(sink, withProps({{"fake-slow", "1500"}})));
        now = 61000;
        c->slowWatch()->check();
        QCOMPARE(w.indicator()->mode(), ValidationIndicator::Slow);
        verify(w.indicator(), "slow");
        QVERIFY(settle(*c));
        QVERIFY(c->editElement(sink, withProps({{"fake-crash", "1"}})));
        QVERIFY(settle(*c));
        QCOMPARE(w.indicator()->mode(), ValidationIndicator::Stopped);
        verify(&w, "stopped");

        // the three setup pages
        for (auto k : {std::array<const char*, 3>{"1.20.3", "1", ""}, {"1.14.5", "", ""}, {"1.20.3", "", "1"}}) {
            env(k[0], k[1], k[2]);
            MainWindow bad(c.get());
            bad.show();
            QVERIFY(waitFor([&] { return bad.showingSetup(); }));
            verify(&bad, QString("setup ") + k[0]);
        }
    }

    // Preview status is visible while the document remains editable.
    void indicatorShowsDuringASlowUpdateAndCanvasRemainsEditable() {
        MainWindow w(c.get()); w.show();
        const QString src = addSource(*c, "videotestsrc");
        w.scene()->rebuild(); w.view()->fitAll();
        QVERIFY(c->editElement(src, withProps({{"fake-slow", "1500"}})));
        QVERIFY(waitFor([&] { return w.indicator()->isVisible(); }, 2000));
        QCOMPARE(w.indicator()->mode(), ValidationIndicator::Validating);
        QVERIFY(w.indicator()->findChild<Spinner*>()->animating());
        QVERIFY(!w.scene()->locked()); QVERIFY(!w.view()->dimmed());
        QVERIFY(w.findChild<QAction*>("actionAddSource")->isEnabled());
        QVERIFY(w.findChild<QLabel*>("stateLabel")->text().contains("keep editing"));
        w.scene()->rebuild();
        QVERIFY(w.scene()->element(src)->flags() & QGraphicsItem::ItemIsMovable);
        const auto revision = c->pipeline().lastChange().executionRevision;
        c->setPosition(src, -9000, 200);
        QCOMPARE(c->pipeline().lastChange().executionRevision, revision);
        const double zoom = w.view()->zoom(); w.view()->zoomBy(1.25);
        QVERIFY(w.view()->zoom() != zoom);
        QVERIFY(settle(*c));
        QVERIFY(!w.indicator()->isVisible());
        QCOMPARE(c->plan().find(src.toStdString())->x, -9000.0);
    }

    void updateGetsTheLargeBackstopTimeout() {
        qunsetenv("GPS_UPDATE_TIMEOUT_MS");
        Controller fresh;
        fresh.loadCatalog();
        QVERIFY(waitFor([&] { return fresh.catalogLoaded(); }));
        QSignalSpy log(&fresh, &Controller::logLine);
        need(fresh, "videotestsrc");
        QVERIFY(fresh.addSource("videotestsrc", {}));
        QVERIFY(settle(fresh));
        bool seen = false;
        for (const auto& args : log) seen |= args[0].toString().contains("timeout-ms=600000");
        QVERIFY(seen);
    }

    // ---- 3. slow validation: the 60 s question, Wait, Request support, Cancel

    void slowQuestionWaitSupportAndCancelKillTheHelper() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString pidFile = work.filePath("slow.pid");
        qputenv("GPS_FAKE_PIDFILE", pidFile.toUtf8());
        QFile::remove(pidFile);
        qint64 now = 0;
        c->slowWatch()->setClock([&now] { return now; });
        auto* ind = w.indicator();

        QVERIFY(c->editElement(src, withProps({{"fake-slow", "30000"}})));
        QVERIFY(waitFor([&] { return readPid(pidFile) > 0; }, 5000));
        qint64 pid = readPid(pidFile);
        QVERIFY(pidAlive(pid));

        now = SlowWatch::kFirstLimitMs - 1;
        c->slowWatch()->check();
        QCOMPARE(ind->mode(), ValidationIndicator::Validating);
        QCOMPARE(ind->findChild<QLabel*>("indicatorElapsed")->text(), QString("%1 s").arg((SlowWatch::kFirstLimitMs - 1) / 1000));
        now = SlowWatch::kFirstLimitMs;
        c->slowWatch()->check();
        QCOMPARE(ind->mode(), ValidationIndicator::Slow);
        QCOMPARE(ind->findChild<QLabel*>("indicatorText")->text(), QString("Validation is taking longer than expected"));
        for (const char* b : {"waitButton", "cancelValidationButton", "supportButton"}) QVERIFY2(ind->findChild<QPushButton*>(b)->isVisible(), b);
        QVERIFY(c->slowAsked());

        // Wait: asked again 15 s later, not before.
        QTest::mouseClick(ind->findChild<QPushButton*>("waitButton"), Qt::LeftButton);
        QCOMPARE(ind->mode(), ValidationIndicator::Validating);
        now = SlowWatch::kFirstLimitMs + SlowWatch::kExtraMs - 1;
        c->slowWatch()->check();
        QCOMPARE(ind->mode(), ValidationIndicator::Validating);
        now = SlowWatch::kFirstLimitMs + SlowWatch::kExtraMs;
        c->slowWatch()->check();
        QCOMPARE(ind->mode(), ValidationIndicator::Slow);

        // Request support: the URL goes through the opener and says what was going on.
        QTest::mouseClick(ind->findChild<QPushButton*>("supportButton"), Qt::LeftButton);
        QCOMPARE(capture.urls.size(), 1);
        QCOMPARE(QUrlQuery(capture.urls[0]).queryItemValue("title", QUrl::FullyDecoded), QString("Validation is taking longer than expected"));
        QString body = QUrlQuery(capture.urls[0]).queryItemValue("body", QUrl::FullyDecoded);
        QVERIFY(body.contains("Edited the properties of " + src + " (videotestsrc)"));
        QVERIFY(body.contains("GStreamer: 1.20.3"));
        QVERIFY(body.contains("fake gps-update"));  // the log tail

        // Cancel: the helper is gone, the plan keeps the change, Save and Run say why they are off.
        QSignalSpy finished(c.get(), &Controller::updateFinished);
        QTest::mouseClick(ind->findChild<QPushButton*>("cancelValidationButton"), Qt::LeftButton);
        QVERIFY(waitFor([&] { return finished.count() == 1; }, 5000));
        QCOMPARE(finished[0][0].toBool(), false);
        QVERIFY2(!pidAlive(pid), "the helper is still running after Cancel");
        QVERIFY(c->notValidated());
        QVERIFY(!c->busy());
        QCOMPARE(c->plan().find(src.toStdString())->props.at("fake-slow"), std::string("30000"));
        QVERIFY(!ind->isVisible());
        QVERIFY(!w.findChild<QAction*>("actionSave")->isEnabled());
        QVERIFY(!w.findChild<QAction*>("actionRun")->isEnabled());
        QVERIFY(w.findChild<QAction*>("actionSave")->toolTip().contains("Validation was cancelled — press Validate"));
        QVERIFY(w.findChild<QAction*>("actionRun")->toolTip().contains("Validation was cancelled — press Validate"));
        QVERIFY(w.findChild<QLabel*>("stateLabel")->text().contains("Validation was cancelled — press Validate"));
        QVERIFY(w.findChild<QAction*>("actionUpdate")->isEnabled());
        QVERIFY(w.logPanel()->toPlainText().contains("Validation cancelled"));
        QString error;
        QVERIFY(!c->save(work.filePath("x.conf"), &error));
        QVERIFY(error.contains("cancelled"));
        // The old pad information stays, dimmed.
        w.scene()->rebuild();
        QVERIFY(w.scene()->element(src)->opacity() < 1.0);

        // Pressing Validate runs it again (without the slow switch it finishes).
        QVERIFY(c->editElement(src, {}));
        QVERIFY(settle(*c));
        QVERIFY(!c->notValidated());
        QVERIFY(c->canSave());
        QVERIFY(w.findChild<QAction*>("actionSave")->isEnabled());
    }

    void aHelperThatIgnoresSigtermIsKilledAfterTwoSeconds() {
        QString src = addSource(*c, "videotestsrc");
        QString pidFile = work.filePath("stubborn.pid");
        QFile::remove(pidFile);
        qputenv("GPS_FAKE_PIDFILE", pidFile.toUtf8());
        qputenv("GPS_FAKE_IGNORE_TERM", "1");
        QVERIFY(c->editElement(src, withProps({{"fake-slow", "30000"}})));
        QVERIFY(waitFor([&] { return readPid(pidFile) > 0; }, 5000));
        qint64 pid = readPid(pidFile);
        QTest::qWait(200);  // the helper installs its SIGTERM handler right after it wrote the pid
        QElapsedTimer timer;
        timer.start();
        c->cancelUpdate();
        QVERIFY(c->cancelling());
        QVERIFY(waitFor([&] { return !c->busy(); }, 8000));
        QVERIFY2(timer.elapsed() >= 1900, qPrintable(QString::number(timer.elapsed())));  // SIGTERM alone did nothing
        QVERIFY2(timer.elapsed() < 4500, qPrintable(QString::number(timer.elapsed())));   // SIGKILL came
        QVERIFY(!pidAlive(pid));
        QVERIFY(c->notValidated());
    }

    // ---- helper crash

    void aCrashedValidatorShowsTheMessageWithSupportAndRetry() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString bad = addAfter(*c, src, "src", "fakesink", "sink", withProps({{"fake-crash", "1"}}));
        QCOMPARE(c->result().status, QString("crashed"));
        auto* ind = w.indicator();
        QVERIFY(ind->isVisible());
        QCOMPARE(ind->mode(), ValidationIndicator::Stopped);
        QCOMPARE(ind->findChild<QLabel*>("indicatorText")->text(), QString("The validator stopped unexpectedly"));
        QVERIFY(ind->findChild<QPushButton*>("supportButton")->isVisible());
        QVERIFY(ind->findChild<QPushButton*>("retryValidationButton")->isVisible());
        QVERIFY(!ind->findChild<Spinner*>()->isVisible());
        QVERIFY(w.updatePanel()->findChild<QPushButton*>("supportButton")->isVisibleTo(&w));  // the Validation tab offers it too

        QTest::mouseClick(ind->findChild<QPushButton*>("supportButton"), Qt::LeftButton);
        QCOMPARE(capture.urls.size(), 1);
        QCOMPARE(QUrlQuery(capture.urls[0]).queryItemValue("title", QUrl::FullyDecoded), QString("The validator stopped unexpectedly"));
        QString body = QUrlQuery(capture.urls[0]).queryItemValue("body", QUrl::FullyDecoded);
        QVERIFY(body.contains(src + ".src (videotestsrc) -> " + bad + ".sink (fakesink)"));
        QVERIFY(body.contains("stopped unexpectedly"));

        // Retry runs the validation again (it crashes again: the element is still there), then a fix clears the message.
        QSignalSpy finished(c.get(), &Controller::updateFinished);
        QTest::mouseClick(ind->findChild<QPushButton*>("retryValidationButton"), Qt::LeftButton);
        QCOMPARE(ind->mode(), ValidationIndicator::Validating);
        QVERIFY(waitFor([&] { return finished.count() == 1; }));
        QCOMPARE(ind->mode(), ValidationIndicator::Stopped);
        QVERIFY(c->editElement(bad, {}));
        QVERIFY(settle(*c));
        QVERIFY(!ind->isVisible());
    }

    void supportFromTheHelpMenuAndTheValidationTab() {
        MainWindow w(c.get());
        w.show();
        w.findChild<QAction*>("actionSupport")->trigger();
        QCOMPARE(capture.urls.size(), 1);
        QVERIFY(capture.urls[0].toString().startsWith("https://github.com/mevlt01001/GstPipeStudio/issues/new?title="));
        QString src = addSource(*c, "videotestsrc");
        addAfter(*c, src, "src", "fakesink", "sink", withProps({{"fake-error", "no way"}}));
        QTest::mouseClick(w.updatePanel()->findChild<QPushButton*>("supportButton"), Qt::LeftButton);
        QCOMPARE(capture.urls.size(), 2);
        QVERIFY(QUrlQuery(capture.urls[1]).queryItemValue("body", QUrl::FullyDecoded).contains("no way"));
    }

    // ---- 5. first run

    void theFourStartupStates() {
        StartupResult r = check();
        QCOMPARE(r.state, StartupResult::Ready);
        QCOMPARE(r.version, QString("1.20.3"));
        QVERIFY(r.nvidia && r.nvidiaElements > 0);
        QCOMPARE(r.statusText(), QString("GStreamer 1.20.3 · NVIDIA elements: %1").arg(r.nvidiaElements));

        env("1.20.3", "1", "");
        QCOMPARE(check().state, StartupResult::NotInstalled);
        env("1.14.5", "", "");
        r = check();
        QCOMPARE(r.state, StartupResult::TooOld);
        QCOMPARE(r.version, QString("1.14.5"));
        env("1.20.3", "", "1");
        QCOMPARE(check().state, StartupResult::HelperBroken);

        // The helper cannot run, and gst-inspect says the version is old: that is why, so TooOld.
        env("1.14.5", "", "1");
        r = check();
        QCOMPARE(r.state, StartupResult::TooOld);
        QCOMPARE(r.version, QString("1.14.5"));

        // The minimum is one constant: 1.16.0 passes, 1.15.9 does not.
        env("1.16.0", "", "");
        QCOMPARE(check().state, StartupResult::Ready);
        env("1.15.9", "", "");
        QCOMPARE(check().state, StartupResult::TooOld);
        QCOMPARE(kMinGstVersion.major * 100 + kMinGstVersion.minor, 116);

        // No helper at all in the folder: nothing to ask, nothing installed.
        env("1.20.3", "", "");
        QTemporaryDir empty;
        QByteArray old = qgetenv("GPS_BIN_DIR");
        qputenv("GPS_BIN_DIR", empty.path().toUtf8());
        QCOMPARE(check().state, StartupResult::NotInstalled);
        qputenv("GPS_BIN_DIR", old);
    }

    void theSetupPageShowsForEveryBadStateAndRetryRecovers() {
        env("1.20.3", "1", "");
        MainWindow w(c.get());
        w.show();
        QVERIFY(waitFor([&] { return w.showingSetup(); }));
        auto* page = w.setupPage();
        QCOMPARE(page->findChild<QLabel*>("setupTitle")->text(), QString("GStreamer was not found"));
        QCOMPARE(page->command(), QString("sudo apt install gstreamer1.0-tools gstreamer1.0-plugins-base gstreamer1.0-plugins-good"));
        QVERIFY(page->findChild<QLabel*>("setupCommand")->text().contains("sudo apt install"));
        QVERIFY(!w.findChild<QAction*>("actionAddSource")->isEnabled());  // with the reason in its tooltip
        QVERIFY(w.findChild<QAction*>("actionAddSource")->toolTip().contains("GStreamer is not ready"));
        QVERIFY(w.findChild<QLabel*>("environmentLabel")->text().isEmpty());

        QTest::mouseClick(page->findChild<QPushButton*>("copyCommandButton"), Qt::LeftButton);
        QCOMPARE(QGuiApplication::clipboard()->text(), page->command());
        // Retry while it is still missing: the page stays.
        QTest::mouseClick(page->findChild<QPushButton*>("retryButton"), Qt::LeftButton);
        QTest::qWait(300);
        QVERIFY(w.showingSetup());

        // Too old
        env("1.14.5", "", "");
        QTest::mouseClick(page->findChild<QPushButton*>("retryButton"), Qt::LeftButton);
        QVERIFY(waitFor([&] { return page->findChild<QLabel*>("setupTitle")->text() == "GStreamer is too old"; }));
        QCOMPARE(page->findChild<QLabel*>("setupText")->text(), QString("GStreamer 1.14.5 was found, GstPipeStudio needs 1.16 or newer on this system."));
        QVERIFY(!page->findChild<QLabel*>("setupCommand")->text().contains("apt"));  // nothing that could break the system

        // Broken helper
        env("1.20.3", "", "1");
        QTest::mouseClick(page->findChild<QPushButton*>("retryButton"), Qt::LeftButton);
        QVERIFY(waitFor([&] { return page->findChild<QLabel*>("setupTitle")->text().contains("helper could not start"); }));
        QCOMPARE(page->command(), QString("gps-inspect version"));

        // Fixed: Retry brings the canvas back with the status text.
        env("1.20.3", "", "");
        QTest::mouseClick(page->findChild<QPushButton*>("retryButton"), Qt::LeftButton);
        QVERIFY(waitFor([&] { return !w.showingSetup(); }));
        QVERIFY(w.findChild<QAction*>("actionAddSource")->isEnabled());
        QVERIFY(w.findChild<QLabel*>("environmentLabel")->text().startsWith("GStreamer 1.20.3 · NVIDIA elements: "));
    }

    void aWorkingEnvironmentGoesStraightToTheCanvas() {
        MainWindow w(c.get());
        w.show();
        QVERIFY(waitFor([&] { return !w.findChild<QLabel*>("environmentLabel")->text().isEmpty(); }));
        QVERIFY(!w.showingSetup());
        QVERIFY(w.findChild<QLabel*>("environmentLabel")->text().startsWith("GStreamer 1.20.3"));
    }

    // ---- spinner

    void theSpinnerTurnsIsGreenAndStopsWhenHidden() {
        Spinner s;
        s.show();
        QTest::qWait(30);
        QImage first = s.grab().toImage();
        QTest::qWait(140);
        QImage second = s.grab().toImage();
        QVERIFY(first != second);                          // it turned
        QVERIFY2(accentPixels(first) > 15, "no accent arc");  // painted, not text characters
        QVERIFY(accentPixels(second) > 15);
        QVERIFY(s.animating());
        s.hide();
        QVERIFY(!s.animating());                           // no timer while it is not visible
        s.show();
        QVERIFY(s.animating());
    }

    // ---- theme

    void themeColoursMeetTheContrastRules() {
        const QColor surfaces[] = {Theme::background(), Theme::panel(), Theme::box()};
        for (const QColor& surface : surfaces) {
            QVERIFY2(Theme::contrast(Theme::text(), surface) >= 4.5, qPrintable("text on " + surface.name()));
            QVERIFY2(Theme::contrast(Theme::muted(), surface) >= 4.5, qPrintable("muted on " + surface.name()));
            QVERIFY2(Theme::contrast(Theme::error(), surface) >= 4.5, qPrintable("error on " + surface.name()));
            QVERIFY2(Theme::contrast(Theme::warning(), surface) >= 4.5, qPrintable("warning on " + surface.name()));
            QVERIFY2(Theme::contrast(Theme::accent(), surface) >= 3.0, qPrintable("accent on " + surface.name()));
            QVERIFY2(Theme::contrast(Theme::focusRing(), surface) >= 3.0, qPrintable("focus ring on " + surface.name()));
            QVERIFY2(Theme::contrast(Theme::line(), Theme::background()) > 1.0, "borders");
        }
        QVERIFY(Theme::contrast(Theme::onAccent(), Theme::accent()) >= 4.5);
        QVERIFY(Theme::contrast(Theme::onAccent(), Theme::accentHover()) >= 4.5);
        QVERIFY(Theme::contrast(QColor("#ffffff"), QColor("#000000")) > 20.9);  // the formula itself: 21:1
        QVERIFY(Theme::contrast(QColor("#777777"), QColor("#ffffff")) > 4.4 && Theme::contrast(QColor("#777777"), QColor("#ffffff")) < 4.6);
        // The style sheet really draws the 2 px focus ring and dark text on the green buttons.
        QVERIFY(Theme::styleSheet().contains("QPushButton:focus { border: 2px solid " + Theme::focusRing().name()));
        QVERIFY(Theme::styleSheet().contains("QPushButton#saveButton, QPushButton[accent=\"true\"] { background: " + Theme::accent().name() + "; color: " + Theme::onAccent().name()));
    }

    // ---- window rules

    void windowKeepsItsMinimumSizeAndRemembersItsLayout() {
        QSettings().clear();
        {
            MainWindow w(c.get());
            QCOMPARE(w.minimumSize(), QSize(960, 600));
            w.show();
            w.resize(960, 700);
            QTest::qWait(50);
            w.close();
        }
        MainWindow again(c.get());
        QCOMPARE(again.size(), QSize(960, 700));  // the default is 1360 x 860
        QVERIFY(QSettings().contains("window/state"));
        // The last folder is remembered too.
        QTemporaryDir dir;
        QString path = dir.filePath("p.conf");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("");
        f.close();
        again.openFile(path);
        QCOMPARE(QSettings().value("lastDir").toString(), dir.path());
    }

    void keyboardShortcutsFollowTheRules() {
        MainWindow w(c.get());
        auto keys = [&](const char* name) {
            QStringList out;
            for (const QKeySequence& k : w.findChild<QAction*>(name)->shortcuts()) out << k.toString(QKeySequence::PortableText);
            return out;
        };
        QCOMPARE(keys("actionNew"), QStringList({"Ctrl+N"}));
        QCOMPARE(keys("actionOpen"), QStringList({"Ctrl+O"}));
        QCOMPARE(keys("actionSave"), QStringList({"Ctrl+S"}));
        QCOMPARE(keys("actionSaveAs"), QStringList({"Ctrl+Shift+S"}));
        QCOMPARE(keys("actionFind"), QStringList({"Ctrl+F"}));
        QVERIFY(keys("actionUpdate").contains("Ctrl+R"));
        QVERIFY(keys("actionRun").size() == 1);
        // Menus have access keys.
        QStringList menus;
        for (QAction* a : w.findChild<QMenuBar*>("applicationMenu")->actions()) menus << a->text();
        QCOMPARE(menus, QStringList({"&File", "&Pipeline", "&View", "&Help"}));
    }

    void openingTheDrawerKeepsTheCanvasWhereItWas() {
        MainWindow w(c.get());
        w.show();
        buildSomething();
        w.scene()->rebuild();
        w.view()->fitAll();
        w.view()->zoomBy(1.5);
        int h = w.view()->horizontalScrollBar()->value(), v = w.view()->verticalScrollBar()->value();
        double zoom = w.view()->zoom();
        w.flows()->chooseSource();
        QVERIFY(waitFor([&] { return w.drawer()->width() == Drawer::kWidth; }, 2000));
        QCOMPARE(w.view()->horizontalScrollBar()->value(), h);
        QCOMPARE(w.view()->verticalScrollBar()->value(), v);
        QCOMPARE(w.view()->zoom(), zoom);
        w.drawer()->dismiss();
        QVERIFY(waitFor([&] { return w.drawer()->width() == 0 && !w.drawer()->isVisible(); }, 2000));
        QCOMPARE(w.view()->horizontalScrollBar()->value(), h);
        QVERIFY(!w.drawer()->page());  // the pages are gone
    }

    // Closing properties while validation runs: no caps page opens afterwards.
    void closingTheDrawerDuringTheValidationDropsTheCapsStep() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        w.scene()->rebuild();
        w.view()->fitAll();
        QTest::mouseClick(w.view()->viewport(), Qt::LeftButton, Qt::NoModifier, w.view()->mapFromScene(w.scene()->pad(src, "src", true)->center()));
        auto* picker = pageOf(w, "pickerPage");
        QVERIFY(picker);
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "videoconvert");
        QTest::keyClick(picker, Qt::Key_Return);
        auto* props = pageOf(w, "propertyPage");
        QVERIFY(props);
        QVERIFY(!w.drawer()->isOpen());  // right drawer closed as soon as the element was picked
        qputenv("GPS_FAKE_SLOW_MS", "1200");
        QTest::mouseClick(props->findChild<QPushButton*>("saveButton"), Qt::LeftButton);
        QVERIFY(c->updating());
        w.flows()->closeProperties();
        QVERIFY(waitFor([&] { return !c->busy(); }, 8000));
        QTest::qWait(300);
        QVERIFY(!w.drawer()->isOpen());
        QVERIFY(!w.flows()->capsPage());
        QCOMPARE(c->plan().elements.size(), size_t(2));  // the element stays
        qunsetenv("GPS_FAKE_SLOW_MS");
    }

private:
    void buildSomething() {
        QString src = addSource(*c, "videotestsrc");
        addAfter(*c, src, "src", "videoconvert");
    }

    DrawerPage* pageOf(MainWindow& w, const char* objectName) {
        DrawerPage* found = nullptr;
        (void)QTest::qWaitFor([&] {
            if (QString(objectName) == "propertyPage") found = w.flows()->propertyPage();
            else if (QString(objectName) == "capsPage") found = w.flows()->capsPage();
            else found = w.drawer()->page();
            return found && found->objectName() == objectName;
        }, 8000);
        return found && found->objectName() == objectName ? found : nullptr;
    }
};

QTEST_MAIN(UxTest)
#include "ux_test.moc"
