// widgets_test.cpp – the widgets driven with QTest mouse and keyboard events (offscreen), on top of the fake helpers.
#include <QAction>
#include <QEnterEvent>
#include <QScrollArea>
#include <QCheckBox>
#include <QComboBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QMessageBox>
#include <QStatusBar>

#include "canvasscene.h"
#include "canvasview.h"
#include "capspage.h"
#include "drawer.h"
#include "elementitem.h"
#include "document.h"
#include "documenttabs.h"
#include "documentbackup.h"
#include "documentregistry.h"
#include "backupservice.h"
#include "explorer.h"
#include "explorerentry.h"
#include "explorersource.h"
#include "fileoperations.h"
#include "projectfile.h"
#include "tabclosebutton.h"
#include <QTabBar>
#include <QTreeWidget>
#include <QPointer>
#include "newprojectdialog.h"
#include "panels.h"
#include "templateframeitem.h"
#include "templatepage.h"
#include "templateparamspage.h"
#include "templates.h"
#include "templatepeek.h"
#include "factorydelegate.h"
#include "flows.h"
#include "capslabel.h"
#include "linkitem.h"
#include "logpanel.h"
#include "commandset.h"
#include "contextmenus.h"
#include "mainwindow.h"
#include "saveflow.h"
#include "paditem.h"
#include "pickerpage.h"
#include "propertypage.h"
#include "testsupport.h"
#include "theme.h"
#include "buildinfo.h"
#include "aboutdialog.h"
#include "connectionpreview.h"
#include <QMovie>
#include "telemetrypanel.h"
#include "updatepanel.h"

using namespace support;

namespace {

template <class T>
T* visibleChild(QWidget* parent) {
    for (T* d : parent->findChildren<T*>()) {
        if (d->isVisible()) return d;
    }
    return nullptr;
}

template <class T>
T* shown(QWidget* parent) {
    T* found = nullptr;
    if (!QTest::qWaitFor([&] { return (found = visibleChild<T>(parent)) != nullptr; }, 8000)) return nullptr;
    return found;
}

// The active page: PickerPage in the right drawer, PropertyPage/CapsPage in the bottom dock tabs.
template <class T>
T* pageOf(MainWindow& w, int ms = 8000) {
    T* found = nullptr;
    (void)QTest::qWaitFor([&] {
        if constexpr (std::is_same_v<T, PropertyPage>) found = w.flows()->propertyPage();
        else if constexpr (std::is_same_v<T, CapsPage>) found = w.flows()->capsPage();
        else if constexpr (std::is_same_v<T, TemplatePage>) found = w.propertiesTab()->findChild<TemplatePage*>();
        else found = w.drawer()->page<T>();
        return found != nullptr;
    }, ms);
    return found;
}

void press(QWidget* dialog, const char* buttonName) {
    auto* button = dialog->findChild<QPushButton*>(buttonName);
    QVERIFY2(button, buttonName);
    QTest::mouseClick(button, Qt::LeftButton);
}

// A real double click is press, release, double-click event; QTest::mouseDClick sends only the last.
void doubleClick(QWidget* widget, const QPoint& at) {
    QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier, at);
    QTest::mouseDClick(widget, Qt::LeftButton, Qt::NoModifier, at);
}

// The words of the open menu's actions, and the menu closed.
QStringList takeMenu(QWidget* window) {
    QStringList offered;
    QMenu* menu = shown<QMenu>(window);
    if (!menu) return offered;
    for (QAction* a : menu->actions()) {
        if (!a->isSeparator()) offered << a->text();
    }
    menu->close();
    return offered;
}

// The action of a menu by its object name: the menus show the window's commands, which belong to the window.
QAction* actionOf(QMenu* menu, const char* name) {
    for (QAction* a : menu->actions()) {
        if (a->objectName() == name) return a;
    }
    return nullptr;
}

// Triggers an action of the open menu. A dialog opened by the action closes the popup, so look at the menu first.
void choose(QWidget* window, const char* action) {
    QPointer<QMenu> menu = visibleChild<QMenu>(window);
    QVERIFY2(menu, "no menu is open");
    QAction* a = actionOf(menu, action);
    QVERIFY2(a, action);
    a->trigger();
    if (menu) menu->close();
}

// The next modal dialog that opens, once: act gets it (a message box, New project …).
void whenModal(QObject* owner, std::function<void(QWidget*)> act) {
    auto* poll = new QTimer(owner);
    poll->setInterval(20);
    QObject::connect(poll, &QTimer::timeout, [poll, act] {
        QWidget* modal = QApplication::activeModalWidget();
        if (!modal) return;
        poll->stop();
        poll->deleteLater();
        act(modal);
    });
    poll->start();
}

// The next message box: its button of that object name is clicked, Cancel when it has none.
void answer(QObject* owner, const char* button, QString* asked = nullptr) {
    whenModal(owner, [button, asked](QWidget* modal) {
        auto* box = qobject_cast<QMessageBox*>(modal);
        if (!box) return;
        if (asked) *asked = box->text() + "\n" + box->informativeText();
        if (QPushButton* named = box->findChild<QPushButton*>(button)) named->click();
        else box->button(QMessageBox::Cancel)->click();
    });
}

QPoint viewPoint(CanvasView* view, const QPointF& scenePoint) { return view->mapFromScene(scenePoint); }

QString text(const std::string& s) { return QString::fromStdString(s); }

PropertyPageInput inputFor(const ElementInfo* info) {
    PropertyPageInput in;
    in.info = *info;
    return in;
}

template <class W>
W* in(QWidget& page, const QString& objectName) {
    auto* row = page.findChild<QWidget*>(objectName);
    return row ? row->findChild<W*>() : nullptr;
}

}  // namespace

Q_DECLARE_METATYPE(PickerChoice)

class WidgetsTest : public QObject {
    Q_OBJECT

    std::unique_ptr<Controller> c;

    // Source -> convert -> sink on a window that is shown and fitted.
    void buildChain(MainWindow& w, QString* src, QString* convert, QString* sink) {
        *src = addSource(*c, "videotestsrc");
        *convert = addAfter(*c, *src, "src", "videoconvert");
        *sink = addAfter(*c, *convert, "src", "fakesink");
        w.scene()->rebuild();
        w.view()->fitAll();
    }

private slots:
    void canvasExpandsBeyondTheOriginalRectangle() {
        auto controller = makeController();
        CanvasScene scene(controller.get());
        CanvasView view(&scene);
        view.resize(800, 600);
        view.show();
        const QRectF distant(-50000, -40000, 800, 600);
        view.ensureSpace(distant);
        QVERIFY(scene.sceneRect().contains(distant));
        const QRectF grown = scene.sceneRect();
        scene.rebuild();
        QVERIFY(scene.sceneRect().contains(grown));
        QCOMPARE(&scene.pipeline(), &controller->pipeline());
    }

    void initTestCase() {
        isolateSettings();
        Theme::apply(*qApp);  // widgets are tested the way they look
    }
    void init() { c = makeController(); }
    void cleanup() { c.reset(); }

    void singleElementContextMenuCopiesAndPastes() {
        MainWindow w(c.get()); w.show();
        const QString source = addSource(*c, "videotestsrc");
        w.scene()->rebuild();
        w.contextMenus()->element(*w.front(), source, QPoint(10, 10));
        choose(&w, "actionCopy");
        gps::Graph copied; std::string error;
        QVERIFY(gps::Graph::fromConf(QGuiApplication::clipboard()->text().toStdString(), &copied, &error));
        QCOMPARE(copied.elements.size(), size_t(1));
        QVERIFY(copied.links.empty());
        w.contextMenus()->element(*w.front(), source, QPoint(10, 10));
        choose(&w, "actionPaste");
        QCOMPARE(c->plan().elements.size(), size_t(2));
        QVERIFY(c->undo());
        QCOMPARE(c->plan().elements.size(), size_t(1));
    }

    void liveMetricsPanelShowsSamplesAndStops() {
        MainWindow w(c.get()); w.show();
        QString source = addSource(*c, "videotestsrc");
        QString middle = addAfter(*c, source, "src", "videoconvert");
        addAfter(*c, middle, "src", "fakesink");
        QVERIFY(settle(*c));
        auto* panel = w.findChild<TelemetryPanel*>("telemetryPanel");
        QVERIFY(panel);
        w.panels()->show(panel);
        auto* toggle = panel->findChild<QPushButton*>("monitorToggle");
        QVERIFY(toggle->isEnabled());
        QTest::mouseClick(toggle, Qt::LeftButton);
        QVERIFY(c->monitoring());
        auto* table = panel->findChild<QTableWidget*>("monitorTable");
        QVERIFY(QTest::qWaitFor([&] { return table->rowCount() == 3; }, 3000));
        QCOMPARE(table->item(0, 1)->text(), QString("—"));
        QVERIFY(table->item(1, 1)->text().toDouble() >= 12.0);
        QCOMPARE(table->item(1, 3)->text(), QString("—"));
        QCOMPARE(table->item(2, 3)->text(), QString("10.0"));
        QCOMPARE(panel->findChild<QComboBox*>("monitorElement")->currentText(), QString("All elements"));
        QCOMPARE(panel->findChild<QComboBox*>("monitorElement")->count(), 4);
        QVERIFY(QTest::qWaitFor([&] {
            ElementItem* item = w.scene()->element(middle);
            return item && item->boundingRect().height() > item->view().height() + 30;
        }, 2000));
        QVERIFY(!c->canUndo());
        QTest::mouseClick(toggle, Qt::LeftButton);
        QVERIFY(QTest::qWaitFor([&] { return !c->monitoring(); }, 3000));
    }

    // ---- Theme

    void propertyResetRestoresDefaults() {
        PropertyInfo info; info.name = "gain"; info.kind = "int";
        info.writable = true; info.defaultValue = "5"; info.min = "0"; info.max = "100";
        PropertyEditor editor(info, QString("42"));
        QVERIFY(!editor.findChild<QToolButton*>("resetPropertyButton")->isHidden());  // not the default: ↺ offered
        editor.findChild<QToolButton*>("resetPropertyButton")->click();
        QVERIFY(editor.findChild<QToolButton*>("resetPropertyButton")->isHidden());
        QCOMPARE(editor.text(), QString("5"));
        QVERIFY(editor.touched());
        QVERIFY(PropertyEditor::collect({&editor}, {{"gain", "42"}}).empty());
    }

    void expandedTemplateHasAFrameWithItsPadsOnTheBorder() {
        QTemporaryDir library;
        useUserDir(library.path());
        MainWindow w(c.get());
        w.resize(1400, 900);
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        QVERIFY(saveTemplate(*c, "Tail", {convert, sink}));
        QVERIFY(settle(*c));
        const QString id = c->outline().collapsedTemplate(convert);
        QVERIFY(!id.isEmpty());
        QVERIFY(!w.scene()->frame(id));  // a new template is one box

        QSignalSpy aligned(&c->canvasLayout(), &CanvasLayout::aligned);
        w.scene()->frameExpanded(id);
        QVERIFY(c->isTemplateExpanded(id));
        QVERIFY(aligned.wait(30000));
        QTest::qWait(400);  // the boxes slide apart
        scene_rebuild(w);
        TemplateFrameItem* frame = w.scene()->frame(id);
        QVERIFY(frame);
        QCOMPARE(frame->view().title, QString("Tail"));
        for (const QString& m : {convert, sink}) {
            const QRectF box(w.scene()->element(m)->pos(), QSizeF(metrics::kBoxWidth, w.scene()->element(m)->view().height()));
            QVERIFY(frame->frameRect().contains(box));
        }
        QVERIFY(!frame->frameRect().contains(w.scene()->element(src)->pos()));
        // The template's input is on the left border, and the link from outside attaches there.
        PadItem* input = w.scene()->pad(convert, "sink", false);
        QVERIFY(input && input->parentItem() == frame);
        QCOMPARE(input->center().x(), frame->frameRect().left());
        QCOMPARE(w.scene()->links().size(), 2);
        QVERIFY(w.grab().save(shotDir() + "/frame_expanded.png"));

        // Inside it, members are plain elements; the border selects the template as one element.
        frame->setSelected(true);
        const QStringList selected = w.scene()->selectedElementNames();
        QCOMPARE(QSet<QString>(selected.begin(), selected.end()), (QSet<QString>{convert, sink}));
        // Left alone, it waits to collapse; with the pointer inside, it does not.
        QVERIFY(frame->collapsePending());
        frame->pointer(frame->frameRect().center());
        QVERIFY(!frame->collapsePending());

        w.scene()->frameMinimized(id);
        QVERIFY(!c->isTemplateExpanded(id));
        QVERIFY(aligned.wait(30000));
        scene_rebuild(w);
        QVERIFY(!w.scene()->frame(id));
        QCOMPARE(w.scene()->element(convert), w.scene()->element(sink));  // one box again
    }

    // Edit template, from a copy's right click, opens the library template in a tab of its own; what that tab saves is
    // offered to the copies of the other tab when it comes to the front.
    void editTemplateOpensATabOfItsOwn() {
        QTemporaryDir library;
        useUserDir(library.path());
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        QVERIFY(saveTemplate(*c, "Tail", {convert, sink}));
        QVERIFY(settle(*c));
        scene_rebuild(w);
        Document* project = w.front();

        w.scene()->elementContext(convert, QPoint(20, 20));
        QVERIFY(waitFor([&] { return visibleChild<QMenu>(&w) != nullptr; }));
        choose(&w, "actionEditTemplate");
        QCOMPARE(w.documents().size(), 2);
        QCOMPARE(w.tabs()->count(), 2);
        QCOMPARE(w.tabs()->tabText(1), QString("Template: Tail"));
        QCOMPARE(w.tabs()->currentIndex(), 1);
        Controller* other = w.front()->controller();
        QVERIFY(other != c.get());
        QSignalSpy laidOut(&other->canvasLayout(), &CanvasLayout::aligned);  // an opened file is laid out once
        QVERIFY(waitFor([&] { return other->target().libraryTemplate() == "Tail" && other->plan().elements.size() == 2; }, 30000));
        QVERIFY(settle(*other));
        QVERIFY(laidOut.count() || laidOut.wait(30000));
        QVERIFY(waitFor([&] { return !other->canvasLayout().applying(); }));
        QVERIFY(w.windowTitle().contains("Template: Tail"));
        scene_rebuild(w);
        QVERIFY(w.grab().save(shotDir() + "/template_tab.png"));
        // Asking for it again brings the same tab to the front.
        w.activate(project);
        Document* again = w.openTemplate("Tail");
        QCOMPARE(again, w.front());
        QCOMPARE(w.tabs()->currentIndex(), 1);
        QCOMPARE(w.documents().size(), 2);

        // Saved there, the first tab is asked about the newer version once it is in front again.
        QSignalSpy outdated(c.get(), &Controller::templatesOutdated);
        QVERIFY(saved(other->save()));
        QVERIFY(outdated.count() || outdated.wait(5000));
        QString asked;
        auto* poll = new QTimer(&w);
        poll->setInterval(20);
        connect(poll, &QTimer::timeout, [poll, &asked] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (!box) return;
            poll->stop();
            asked = box->text();
            box->button(QMessageBox::Yes)->click();
        });
        poll->start();
        w.activate(project);
        QVERIFY(waitFor([&] { return !asked.isEmpty(); }));
        QVERIFY(asked.contains("Tail: revision 1 → 2"));
        QVERIFY(waitFor([&] { return !holdsOlderTemplates(*c); }));
        useUserDir({});
    }

    // Tabs as editors have them: a dot while a tab has unsaved changes, a file opens once, closing asks to save.
    void tabsMarkUnsavedChangesAndAskBeforeClosing() {
        QTemporaryDir root;
        MainWindow w(c.get());
        w.show();
        const QString src = addSource(*c, "videotestsrc");
        QVERIFY(!src.isEmpty());
        auto dot = [&](int i) { return qobject_cast<TabCloseButton*>(w.tabs()->tabButton(i, QTabBar::RightSide)); };
        QVERIFY(dot(0)->dirty());
        QCOMPARE(dot(0)->text(), QString("●"));
        const QString conf = root.filePath("one.conf");
        QVERIFY(w.saveFlow()->saveAs(w.front(), conf));
        QVERIFY(!dot(0)->dirty());
        QCOMPARE(dot(0)->text(), QString("×"));
        QCOMPARE(w.tabs()->tabText(0), QString("one.conf"));

        // A new tab; the file already open comes to the front instead of opening twice.
        w.findChild<QAction*>("actionNew")->trigger();
        QCOMPARE(w.tabs()->count(), 2);
        QCOMPARE(w.tabs()->tabText(1), QString("Untitled-1"));
        w.findChild<QAction*>("actionNew")->trigger();  // an untouched untitled tab is reused
        QCOMPARE(w.tabs()->count(), 2);
        w.openFile(conf);
        QCOMPARE(w.tabs()->count(), 2);
        QCOMPARE(w.tabs()->currentIndex(), 0);
        w.findChild<QAction*>("actionNextTab")->trigger();
        QCOMPARE(w.tabs()->currentIndex(), 1);
        QVERIFY(w.scene() != w.documents().front()->scene());  // the menus act on the tab in front

        // An unsaved tab asks: Cancel keeps it, Don't save closes it.
        Document* second = w.front();
        QVERIFY(addSource(*second->controller(), "videotestsrc").size());
        QVERIFY(dot(1)->dirty());
        QString asked;
        answer(&w, "cancel", &asked);
        w.findChild<QAction*>("actionCloseTab")->trigger();
        QCOMPARE(w.tabs()->count(), 2);
        QVERIFY(asked.startsWith("Do you want to save the changes you made to Untitled-1?"));
        // Undo back to where it was: nothing to ask about.
        QVERIFY(second->controller()->undo());
        QVERIFY(!dot(1)->dirty());
        QVERIFY(addSource(*second->controller(), "videotestsrc").size());
        answer(&w, "saveChangesDiscard");
        w.findChild<QAction*>("actionCloseTab")->trigger();
        QCOMPARE(w.tabs()->count(), 1);
        QCOMPARE(w.front(), w.documents().front());
        // The last tab closed leaves an untitled one: the window always has a canvas.
        QVERIFY(w.closeDocument(w.front()));
        QCOMPARE(w.tabs()->count(), 1);
        QCOMPARE(w.tabs()->tabText(0), QString("Untitled-1"));
        // The tab that replaces a closed Untitled-1 takes its number again.
        QVERIFY(w.closeDocument(w.front()));
        QCOMPARE(w.tabs()->tabText(0), QString("Untitled-1"));
    }

    // Untitled tabs take the lowest free number; Save of one asks for a project, Save all writes every unsaved tab,
    // and closing the window asks nothing: the changes stay, backed up for the next start (hot exit).
    void untitledTabsSaveAllAndCloseTogether() {
        QTemporaryDir root;
        useUserDir(root.filePath("user"));
        Settings::setProjectsLocation(root.path());
        auto w = std::make_unique<MainWindow>(c.get());
        w->show();
        QCOMPARE(w->tabs()->tabText(0), QString("Untitled-1"));
        QVERIFY(!addSource(*c, "videotestsrc").isEmpty());
        w->findChild<QAction*>("actionNew")->trigger();
        QCOMPARE(w->tabs()->tabText(1), QString("Untitled-2"));
        Document* second = w->front();
        QVERIFY(!addSource(*second->controller(), "videotestsrc").isEmpty());

        // Save of an untitled tab: New project names it, and the first save makes its folder.
        whenModal(w.get(), [](QWidget* modal) {
            QTest::keyClicks(modal->findChild<QLineEdit*>("projectName"), "door");
            press(modal, "createProject");
        });
        w->findChild<QAction*>("actionSave")->trigger();
        QVERIFY(QFile::exists(root.filePath("door/door.conf")));
        QCOMPARE(w->tabs()->tabText(1), QString("door.conf"));
        QVERIFY(!second->controller()->modified());
        QVERIFY(w->saveFlow()->saveAs(w->documents().front(), root.filePath("yard.conf")));

        // Both changed again: Save all writes both, and asks nothing.
        for (Document* d : w->documents()) QVERIFY(!addSource(*d->controller(), "videotestsrc").isEmpty());
        QVERIFY(w->findChild<QAction*>("actionSaveAll")->isEnabled());
        w->findChild<QAction*>("actionSaveAll")->trigger();
        for (Document* d : w->documents()) QVERIFY(!d->controller()->modified());
        refreshed();
        QVERIFY(!w->findChild<QAction*>("actionSaveAll")->isEnabled());

        // Several unsaved tabs closed together are asked about once, in one list.
        for (Document* d : w->documents()) QVERIFY(!addSource(*d->controller(), "videotestsrc").isEmpty());
        QString asked;
        answer(w.get(), "cancel", &asked);
        QVERIFY(!w->saveFlow()->confirmClose(w->documents()));
        QVERIFY(asked.contains("the following 2 files") && asked.contains("yard.conf") && asked.contains("door.conf"));
        // Closing the window asks nothing.
        QVERIFY(w->close());
        for (Document* d : w->documents()) QVERIFY(d->controller()->modified());
        QCOMPARE(Settings::session().tabs.size(), 2);
        Settings::setSession({});
        Settings::setWindow({}, {});  // the next windows start at their own size
        Settings::setProjectsLocation({});
        useUserDir({});
    }

    // A new tab is ready at once: it shares the process's catalog and GStreamer check, and asks for neither again.
    void aNewTabSharesTheCatalogAndNeedsNoCheck() {
        MainWindow w(c.get());
        w.show();
        AppServices& shared = c->services();
        QVERIFY(waitFor([&] { return shared.environment().result().state == StartupResult::Ready; }));
        QVERIFY(!addSource(*c, "videotestsrc").isEmpty());  // the first tab is not pristine: New opens another
        QSignalSpy checked(&shared.environment(), &EnvironmentService::checked);
        QSignalSpy listed(&shared.catalog(), &FactoryCatalog::ready);
        w.findChild<QAction*>("actionNew")->trigger();
        Controller* second = w.front()->controller();
        QVERIFY(second != c.get());
        QCOMPARE(&second->services(), &shared);
        QVERIFY(!w.showingSetup());
        QVERIFY(w.findChild<QAction*>("actionAddElement")->isEnabled());
        // A factory the first tab fetched is known here without asking gps-inspect again.
        QVERIFY(second->addElement("videotestsrc", {}));
        QVERIFY(settle(*second));
        QVERIFY(second->result().ok());
        QTest::qWait(300);
        QCOMPARE(checked.count(), 0);
        QCOMPARE(listed.count(), 0);
    }

    // The View menu sets one preference for every tab, the menu shows it however it was set, and the link style is
    // remembered.
    void theViewMenuSetsOnePreferenceForEveryTab() {
        MainWindow w(c.get());
        w.show();
        ViewPreferences& view = c->services().view();
        auto* rightAngles = w.findChild<QAction*>("actionLinkRightAngles");
        rightAngles->trigger();
        QCOMPARE(view.routing(), layout::Routing::RightAngles);
        QCOMPARE(Settings::linkRouting(), layout::Routing::RightAngles);
        view.setConnectedMove(true);
        support::refreshed();
        QVERIFY(w.findChild<QAction*>("actionConnectedMove")->isChecked());
        view.setConnectedMove(false);
        w.findChild<QAction*>("actionLinkCurves")->trigger();
        QVERIFY(!rightAngles->isChecked());
        QCOMPARE(view.routing(), layout::Routing::Curves);
    }

    // The first tab's controller is the caller's and outlives its tab: what it says afterwards reaches no closed tab.
    void theCallersControllerOutlivesItsClosedTab() {
        MainWindow w(c.get());
        w.show();
        Document* first = w.front();
        Document* second = w.newDocument();
        w.documentTabs()->close(first);  // without asking: the fixture's canvas may hold an earlier test's work
        QCOMPARE(w.documents(), QList<Document*>{second});
        emit c->templatesOutdated({"gone"});
        emit c->changed();
        support::refreshed();
        QCOMPARE(w.front(), second);
        QVERIFY(second->takeOutdated().isEmpty());
    }

    // The tabs open when the window closed come back the next time, the one in front in front.
    void theTabsComeBackOnTheNextStart() {
        QTemporaryDir root, library;
        useUserDir(library.path());
        const QString a = root.filePath("a.conf"), b = root.filePath("b.conf");
        {
            MainWindow w(c.get());
            w.show();
            QVERIFY(!addSource(*c, "videotestsrc").isEmpty());
            QVERIFY(w.saveFlow()->saveAs(w.front(), a));
            QVERIFY(QFile::copy(a, b));
            w.openFile(b);
            QVERIFY(waitFor([&] { return w.front()->controller()->target().path() == b; }, 30000));
            QVERIFY(settle(*w.front()->controller()));
            w.activate(w.documents().front());
            QVERIFY(w.close());
        }
        auto fresh = makeController();
        MainWindow again(fresh.get());
        again.show();
        again.restoreSession();
        QCOMPARE(again.tabs()->count(), 2);
        QVERIFY(waitFor([&] { return again.tabs()->tabText(0) == "a.conf" && again.tabs()->tabText(1) == "b.conf"; }, 30000));
        QCOMPARE(again.tabs()->currentIndex(), 0);
        for (Document* d : again.documents()) QVERIFY(settle(*d->controller()));
        Settings::setSession({});
        Settings::setWindow({}, {});  // the next windows start at their own size
        useUserDir({});
    }

    // About a second after a change, an unsaved tab is backed up, with what it is the backup of; once it is saved,
    // the backup goes.
    void aChangedTabIsBackedUpUntilItIsSaved() {
        QTemporaryDir root;
        useUserDir(root.filePath("user"));
        MainWindow w(c.get());
        w.show();
        QVERIFY(!addSource(*c, "videotestsrc").isEmpty());
        BackupService& backups = c->services().backups();
        QVERIFY(waitFor([&] { return backups.list().size() == 1; }, 3000));
        const Backup backup = backups.list().front();
        QCOMPARE(backup.id, w.front()->backup()->id());
        QCOMPARE(backup.key, QString("untitled:1"));
        QCOMPARE(backup.title, QString("Untitled-1"));
        gps::Graph content;
        std::string error;
        QVERIFY(gps::Graph::load(BackupService::contentOf(backup.id).toStdString(), &content, &error));
        QCOMPARE(content.elements.size(), size_t(1));
        // Undone back to where it was clean, it has nothing to back up; redone, it has again.
        QVERIFY(c->undo());
        QVERIFY(backups.list().isEmpty());
        QVERIFY(c->redo());
        QVERIFY(waitFor([&] { return backups.list().size() == 1; }, 3000));
        QVERIFY(w.saveFlow()->saveAs(w.front(), root.filePath("one.conf")));
        QVERIFY(backups.list().isEmpty());
        // Closed after Don't save, the tab takes its backup with it.
        QVERIFY(!addSource(*c, "audiotestsrc").isEmpty());
        QVERIFY(waitFor([&] { return backups.list().size() == 1; }, 3000));
        answer(&w, "saveChangesDiscard");
        QVERIFY(w.closeDocument(w.front()));
        QVERIFY(backups.list().isEmpty());
        useUserDir({});
    }

    // A file another program changed while the application was closed: its reopened unsaved tab asks Reload or Keep
    // mine, as for any other change, instead of taking the new file for what its changes were based on.
    void aFileChangedWhileClosedIsSaidToItsReopenedTab() {
        QTemporaryDir root;
        useUserDir(root.filePath("user"));
        const QString a = root.filePath("a.conf");
        {
            MainWindow w(c.get());
            w.show();
            const QString src = addSource(*c, "videotestsrc");
            QVERIFY(w.saveFlow()->saveAs(w.front(), a));
            QVERIFY(!addAfter(*c, src, "src", "fakesink").isEmpty());
            QVERIFY(w.close());
        }
        gps::Graph changed;
        changed.addSource("audiotestsrc");
        std::string error;
        QVERIFY(changed.save(a.toStdString(), &error));
        auto fresh = makeController();
        MainWindow again(fresh.get());
        again.show();
        again.restoreSession();
        auto question = [&] { auto* box = again.findChild<QMessageBox*>("changedOnDisk"); return box && box->isVisible() ? box : nullptr; };
        QVERIFY(waitFor([&] { return question(); }, 30000));
        question()->findChild<QPushButton*>("keepMine")->click();
        QCOMPARE(again.front()->controller()->plan().elements.size(), size_t(2));
        Settings::setSession({});
        Settings::setWindow({}, {});
        useUserDir({});
    }

    // Quitting asks nothing; the next start brings back every tab with its unsaved changes: an untitled tab, and a
    // file changed since it was saved. A crash before the window could save its session leaves the same backups.
    void quittingKeepsUnsavedTabsForTheNextStart() {
        QTemporaryDir root;
        useUserDir(root.filePath("user"));
        const QString a = root.filePath("a.conf");
        {
            MainWindow w(c.get());
            w.show();
            const QString src = addSource(*c, "videotestsrc");
            QVERIFY(w.saveFlow()->saveAs(w.front(), a));
            QVERIFY(!addAfter(*c, src, "src", "fakesink").isEmpty());
            w.findChild<QAction*>("actionNew")->trigger();
            QVERIFY(!addSource(*w.front()->controller(), "videotestsrc").isEmpty());
            QVERIFY(w.close());  // a question would block here
        }
        // The tabs a crash leaves come back in the order their backups were written.
        auto reopened = [&](MainWindow& again) {
            again.show();
            again.restoreSession();
            auto tab = [&](const QString& title) -> Controller* {
                for (Document* d : again.documents())
                    if (d->title() == title) return d->controller();
                return nullptr;
            };
            QVERIFY(waitFor([&] { return again.tabs()->count() == 2 && tab("a.conf") && tab("Untitled-1"); }, 30000));
            Controller* file = tab("a.conf");
            Controller* untitled = tab("Untitled-1");
            QVERIFY(settle(*file) && settle(*untitled));
            QCOMPARE(file->plan().elements.size(), size_t(2));
            QCOMPARE(untitled->plan().elements.size(), size_t(1));
            QVERIFY(file->hasUnsavedWork() && untitled->hasUnsavedWork());
            QCOMPARE(file->target().path(), a);
        };
        {
            auto fresh = makeController();
            MainWindow again(fresh.get());
            reopened(again);
            QCOMPARE(again.tabs()->tabText(0), QString("a.conf"));  // the session's order
            QVERIFY(waitFor([&] { return c->services().backups().list().size() == 2; }, 3000));  // the same two, written on
        }  // a crash: the window goes without saving its session
        Settings::setSession({});
        auto fresh = makeController();
        MainWindow afterCrash(fresh.get());
        reopened(afterCrash);
        QCOMPARE(c->services().backups().list().size(), 2);
        Settings::setSession({});
        Settings::setWindow({}, {});
        useUserDir({});
    }

    // A backup no tab takes up again (its content cannot be loaded) is offered a few starts, then it goes; written
    // again by its tab, it starts counting anew.
    void aBackupNoTabTakesUpGoesAfterAFewStarts() {
        QTemporaryDir root;
        useUserDir(root.filePath("user"));
        BackupService& backups = c->services().backups();
        QVERIFY(backups.write({"stale", "untitled:7", "Untitled-7"}, "not a pipeline"));
        QCOMPARE(backups.reopen({}).size(), 1);
        QVERIFY(backups.write({"stale", "untitled:7", "Untitled-7"}, "not a pipeline"));  // its tab wrote it: alive
        for (int start = 1; start <= BackupService::kStartsKept; ++start) QCOMPARE(backups.reopen({}).size(), 1);
        QVERIFY(backups.reopen({}).isEmpty());
        QVERIFY(backups.list().isEmpty());
        // One the session names comes back however many starts its tab fails to load it.
        QVERIFY(backups.write({"named", "untitled:8", "Untitled-8"}, "not a pipeline"));
        Settings::Session session;
        session.tabs << "untitled:8";
        session.backups << "named";
        for (int start = 0; start <= BackupService::kStartsKept; ++start) QCOMPARE(backups.reopen(session).size(), 1);
        QCOMPARE(backups.list().size(), 1);
        useUserDir({});
    }

    // The backups another running application writes are not this one's: not reopened, counted or dropped. Once that
    // application is gone, they are.
    void anotherRunningApplicationsBackupsAreLeftAlone() {
        QTemporaryDir root;
        useUserDir(root.filePath("user"));
        BackupService& mine = services().backups();
        {
            BackupService other;
            QVERIFY(other.write({"theirs", "untitled:3", "Untitled-3"}, "x"));
            Settings::Session session;
            session.tabs << "untitled:3";
            session.backups << "theirs";
            for (int start = 0; start <= BackupService::kStartsKept; ++start) {
                QVERIFY(mine.reopen({}).isEmpty());
                QVERIFY(mine.reopen(session).value(0).id.isEmpty());
            }
            QVERIFY(mine.find("theirs"));
        }
        QCOMPARE(mine.reopen({}).size(), 1);
        useUserDir({});
    }

    // Another program changes a tab's file: a tab without unsaved changes reads it again; one with unsaved changes is
    // not reloaded, the user chooses, and Save over the file asks first.
    void aFileChangedOnDiskReloadsOnlyACleanTab() {
        QTemporaryDir root;
        const QString a = root.filePath("a.conf"), one = root.filePath("one.conf"), two = root.filePath("two.conf");
        auto other = makeController();
        const QString src = addSource(*other, "videotestsrc");
        QVERIFY(saved(saveAs(*other, one)));
        QVERIFY(!addAfter(*other, src, "src", "fakesink").isEmpty());
        QVERIFY(saved(saveAs(*other, two)));
        auto overwrite = [](const QString& file, const QString& with) {
            QFile from(with), to(file);
            return from.open(QIODevice::ReadOnly) && to.open(QIODevice::WriteOnly | QIODevice::Truncate) && to.write(from.readAll()) > 0;
        };
        MainWindow w(c.get());
        w.show();
        QVERIFY(!addSource(*c, "videotestsrc").isEmpty());
        QVERIFY(w.saveFlow()->saveAs(w.front(), a));
        QTest::qWait(2 * DocumentRegistry::kSettleMs);
        QCOMPARE(c->plan().elements.size(), size_t(1));  // its own save is no change

        QVERIFY(overwrite(a, two));
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 2 && !c->busy(); }));
        QVERIFY(!c->modified());
        QVERIFY(w.statusBar()->currentMessage().contains("changed on disk and was reloaded"));

        // Changed here and there: the tab keeps its changes until the user chooses.
        QVERIFY(!addSource(*c, "audiotestsrc").isEmpty());
        QVERIFY(overwrite(a, one));
        QVERIFY(waitFor([&] { auto* box = w.findChild<QMessageBox*>("changedOnDisk"); return box && box->isVisible(); }));
        QCOMPARE(c->plan().elements.size(), size_t(3));
        QVERIFY(c->modified());
        w.findChild<QMessageBox*>("changedOnDisk")->findChild<QPushButton*>("keepMine")->click();
        QCOMPARE(c->plan().elements.size(), size_t(3));

        // Changed on disk again, saved before the watcher tells: Save asks; Cancel writes nothing, Overwrite does.
        QVERIFY(overwrite(a, two));
        answer(&w, "cancel");
        QVERIFY(!w.saveFlow()->save(w.front()));
        QVERIFY(c->modified());
        answer(&w, "overwriteOnDisk");
        QVERIFY(w.saveFlow()->save(w.front()));
        QVERIFY(!c->modified());
        gps::Graph written;
        std::string error;
        QVERIFY(gps::Graph::load(a.toStdString(), &written, &error));
        QCOMPARE(written.elements.size(), size_t(3));

        // Deleted on disk: the tab stays, marked deleted and modified; back as it was (a checkout), it is clean again.
        QVERIFY(QFile::copy(a, root.filePath("kept.conf")));
        QVERIFY(QFile::remove(a));
        QVERIFY(waitFor([&] { return w.front()->title() == "a.conf (deleted)"; }));
        QVERIFY(c->modified());
        QVERIFY(QFile::copy(root.filePath("kept.conf"), a));
        QVERIFY(waitFor([&] { return w.front()->title() == "a.conf" && !c->modified(); }));

        // Deleted again: Save writes it again.
        QVERIFY(QFile::remove(a));
        QVERIFY(waitFor([&] { return w.front()->title() == "a.conf (deleted)"; }));
        QVERIFY(c->modified());
        QVERIFY(w.saveFlow()->save(w.front()));
        QVERIFY(QFile::exists(a) && !c->modified());
        QCOMPARE(w.front()->title(), QString("a.conf"));

        // In the background, a modified tab is asked about once it comes to the front.
        auto question = [&] { auto* box = w.findChild<QMessageBox*>("changedOnDisk"); return box && box->isVisible() ? box : nullptr; };
        QVERIFY(!addSource(*c, "audiotestsrc").isEmpty());
        Document* file = w.front();
        w.newDocument();
        QVERIFY(overwrite(a, one));
        QTest::qWait(4 * DocumentRegistry::kSettleMs);
        QVERIFY(!question());
        w.activate(file);
        QVERIFY(waitFor([&] { return question(); }));
        question()->findChild<QPushButton*>("reloadFromDisk")->click();
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 1 && !c->busy(); }));
        QVERIFY(!c->modified());
    }

    void saveAsTemplateDrawsTheBorderThenAsksForNameAndParameters() {
        QTemporaryDir library;
        useUserDir(library.path());
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        QVERIFY(c->setCaps(convert, "src", "video/x-raw, width=(int)320", {}));
        QVERIFY(settle(*c));
        auto* save = w.findChild<QAction*>("actionSaveTemplateElement");
        w.scene()->setSelectedElements({});
        auto* remove = w.findChild<QAction*>("actionDelete");
        refreshed();
        QVERIFY(!save->isEnabled());  // nothing to save
        QVERIFY(!remove->isEnabled());
        w.scene()->setSelectedElements({convert, sink});
        refreshed();
        QVERIFY(save->isEnabled());
        QVERIFY(remove->isEnabled());
        save->trigger();
        auto* page = pageOf<TemplatePage>(w);
        QVERIFY(page);
        QCOMPARE(c->outline().visibleFrames().size(), 1);  // the border is drawn while the name is asked
        const QString id = c->outline().visibleFrames().front();

        // Add parameter: an element first, then one of its properties or a field of its fixed caps.
        auto* element = page->findChild<QComboBox*>("paramElement");
        auto* property = page->findChild<QComboBox*>("paramProperty");
        QCOMPARE(element->count(), 2);
        QCOMPARE(element->itemText(0), fromStd(c->names().element(toStd(convert))));  // the user reads factories, not unique names
        element->setCurrentIndex(element->findData(convert));
        QVERIFY(property->findText("caps.width") >= 0);
        property->setCurrentText("caps.width");
        press(page, "addParameter");
        element->setCurrentIndex(element->findData(sink));
        property->setCurrentText("sync");
        press(page, "addParameter");
        press(page, "addParameter");  // twice is once
        QCOMPARE(page->findChild<QListWidget*>("templateParams")->count(), 2);

        press(page, "saveButton");  // no name yet
        QVERIFY(page->findChild<QLabel*>("templateError")->isVisible());
        QTest::keyClicks(page->findChild<QLineEdit*>("templateName"), "Tail");
        press(page, "saveButton");
        QVERIFY(waitFor([&] { return !w.flows()->propertyPage() && !visibleChild<TemplatePage>(&w); }));
        QCOMPARE(c->services().library().names(), QStringList({"Tail"}));
        const auto params = c->plan().instance(toStd(id))->params;
        QCOMPARE(params.size(), size_t(2));
        QCOMPARE(params[0].name, c->names().element(toStd(convert)) + ".caps.width");
        QCOMPARE(params[1].name, c->names().element(toStd(sink)) + ".sync");
        QFile file(library.path() + "/data/templates/Tail.conf");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString saved = QString::fromUtf8(file.readAll());
        QVERIFY(saved.contains("[expose:") && saved.contains("caps.width") && !saved.contains("\nx="));
    }

    void cancellingSaveAsTemplateLeavesPlainElements() {
        QTemporaryDir library;
        useUserDir(library.path());
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        w.scene()->setSelectedElements({convert, sink});
        refreshed();
        w.findChild<QAction*>("actionSaveTemplateElement")->trigger();
        auto* page = pageOf<TemplatePage>(w);
        QVERIFY(page);
        QCOMPARE(c->plan().instances.size(), size_t(1));
        // A new template's border is drawn from one corner along both arms; cancelled, it is erased the same way.
        QVERIFY(waitFor([&] { return w.scene()->frames().size() == 1; }));
        QVERIFY(w.scene()->frames().front()->tracing());
        press(page, "cancelButton");
        QVERIFY(waitFor([&] { return c->plan().instances.empty(); }));
        QVERIFY(waitFor([&] { return !w.scene()->ghosts().isEmpty(); }));
        QCOMPARE(w.scene()->ghosts().front()->kind(), FrameGhost::Untrace);
        QVERIFY(c->services().library().names().isEmpty());
        QVERIFY(c->plan().find(toStd(convert))->group.empty());
    }

    void addTemplateListsTheLibraryWithAPeekAndInsertsCollapsed() {
        QTemporaryDir library;
        useUserDir(library.path());
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        QVERIFY(saveTemplate(*c, "Tail", {convert, sink}));
        QVERIFY(settle(*c));

        // Add element lists elements only; templates have their own page.
        w.findChild<QAction*>("actionAddElement")->trigger();
        auto* elements = pageOf<PickerPage>(w);
        QVERIFY(elements);
        for (int i = 0; i < elements->findChild<QListWidget*>("pickerList")->count(); ++i)
            QVERIFY(elements->findChild<QListWidget*>("pickerList")->item(i)->text() != "Templates");
        w.drawer()->dismiss();

        w.findChild<QAction*>("actionAddTemplate")->trigger();
        QVERIFY(waitFor([&] { return w.drawer()->title() == "Add template"; }));
        auto* picker = pageOf<PickerPage>(w);
        QVERIFY(picker);
        auto* list = picker->findChild<QListWidget*>("pickerList");
        QCOMPARE(list->item(0)->text(), QString("Templates"));
        QCOMPARE(list->item(1)->text(), QString("Tail"));
        QVERIFY(waitFor([&] { return w.drawer()->width() == Drawer::kWidth; }, 2000));

        // Hovering a template for a moment shows a peek inside it; moving off hides it.
        const QPoint row = list->visualItemRect(list->item(1)).center();
        QMouseEvent move(QEvent::MouseMove, row, list->viewport()->mapToGlobal(row), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(list->viewport(), &move);
        QVERIFY(waitFor([&] { return TemplatePeek::current() != nullptr; }, 2000));
        QVERIFY(waitFor([&] { return TemplatePeek::current() && TemplatePeek::current()->laidOut(); }, 30000));
        QVERIFY(TemplatePeek::current()->grab().save(shotDir() + "/template_peek.png"));
        QLabel tipped("tipped");  // no tooltip covers an open peek
        tipped.setToolTip("a tooltip");
        tipped.show();
        QHelpEvent tip(QEvent::ToolTip, QPoint(2, 2), tipped.mapToGlobal(QPoint(2, 2)));
        QApplication::sendEvent(&tipped, &tip);
        QVERIFY(!QToolTip::isVisible());
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(list->viewport(), &leave);
        QVERIFY(waitFor([&] { return TemplatePeek::current() == nullptr; }, 2000));

        // Its right click is the library's menu, the Explorer's own (FileOperations).
        emit list->customContextMenuRequested(row);
        QCOMPARE(takeMenu(&w), QStringList({"Edit template", "Rename", "Export…", "Delete"}));

        // Choosing it inserts it collapsed, as one box, and nice alignment finds it a place.
        QSignalSpy aligned(&c->canvasLayout(), &CanvasLayout::aligned);
        list->setCurrentRow(1);
        press(picker, "saveButton");
        QVERIFY(waitFor([&] { return c->plan().instances.size() == 2; }));
        QVERIFY(aligned.wait(30000));
        const QString placed = fromStd(c->plan().instances.back().id);
        QVERIFY(!c->isTemplateExpanded(placed));
        QCOMPARE(c->outline().membersOf(placed).size(), 2);
    }

    // A new project: a name, and its folder under the projects location; Save of a never saved canvas asks the same.
    void aNewProjectGetsAFolderOfItsOwn() {
        QTemporaryDir root;
        Settings::setProjectsLocation(root.path());
        NewProjectDialog dialog;
        auto* create = dialog.findChild<QPushButton*>("createProject");
        QVERIFY(!create->isEnabled());  // no name yet
        QTest::keyClicks(dialog.findChild<QLineEdit*>("projectName"), "front door");
        QCOMPARE(dialog.confPath(), root.filePath("front_door/front_door.conf"));
        QVERIFY(create->isEnabled());
        QVERIFY(QDir().mkpath(root.filePath("front_door")));
        QFile taken(root.filePath("front_door/front_door.conf"));
        QVERIFY(taken.open(QIODevice::WriteOnly));
        taken.close();
        dialog.findChild<QLineEdit*>("projectName")->clear();
        dialog.findChild<QLineEdit*>("projectName")->setText("front door");
        QVERIFY(!create->isEnabled());  // there is one already
        Settings::setProjectsLocation({});

        c->newPlan(std::make_unique<ProjectConfTarget>(root.filePath("back/back.conf")));
        QCOMPARE(c->target().path(), root.filePath("back/back.conf"));
        QVERIFY(c->plan().empty());
    }

    // The Explorer, left of the canvas: the open project's files, every project, and the template library.
    void theExplorerShowsProjectsFilesAndTemplates() {
        QTemporaryDir root, library;
        useUserDir(library.path());
        Settings::setProjectsLocation(root.path());
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        QVERIFY(saveTemplate(*c, "Tail", {convert, sink}));
        QVERIFY(settle(*c));
        const QString conf = root.filePath("door/door.conf");
        QVERIFY(QDir().mkpath(root.filePath("door")));
        QVERIFY(w.saveFlow()->saveAs(w.front(), conf));
        QVERIFY(QDir().mkpath(root.filePath("yard")));
        QVERIFY(QFile::copy(conf, root.filePath("yard/yard.conf")));
        refreshed();

        QCOMPARE(w.panels()->dock(w.explorer())->windowTitle(), QString("Explorer"));
        QCOMPARE(w.dockWidgetArea(w.panels()->dock(w.explorer())), Qt::LeftDockWidgetArea);
        QTreeWidget* tree = w.explorer()->tree();
        auto childTexts = [](QTreeWidgetItem* section) {
            QStringList out;
            for (int i = 0; i < section->childCount(); ++i) out << section->child(i)->text(0);
            return out;
        };
        QVERIFY(waitFor([&] { return childTexts(tree->topLevelItem(1)) == QStringList({"door", "yard"}); }));  // the folder is watched
        QCOMPARE(tree->topLevelItem(0)->text(0), QString("Project · door"));
        QCOMPARE(childTexts(tree->topLevelItem(0)), QStringList({"templates/", "door.conf", "door.gpsproj"}));
        QCOMPARE(childTexts(tree->topLevelItem(0)->child(0)), QStringList({"Tail.conf"}));
        QCOMPARE(childTexts(tree->topLevelItem(2)), QStringList({"Tail"}));
        QCOMPARE(tree->topLevelItem(3)->text(0), QString("Plugins"));  // no plugins yet: it says where they will appear
        QCOMPARE(childTexts(tree->topLevelItem(3)), QStringList({"Custom elements will appear here"}));
        QVERIFY(w.grab().save(shotDir() + "/explorer.png"));

        // A project opens with a double click, in a tab of its own; a template opens in its own tab too.
        emit tree->itemActivated(tree->topLevelItem(1)->child(1), 0);
        QCOMPARE(w.tabs()->count(), 2);
        Controller* yard = w.front()->controller();
        QVERIFY(waitFor([&] { return yard->target().path() == root.filePath("yard/yard.conf"); }, 30000));
        QVERIFY(settle(*yard));
        QVERIFY(waitFor([&] { return tree->topLevelItem(0)->text(0) == "Project · yard"; }));
        emit tree->itemActivated(tree->topLevelItem(2)->child(0), 0);
        QCOMPARE(w.tabs()->count(), 3);
        QCOMPARE(w.front()->controller()->target().libraryTemplate(), QString("Tail"));
        // A template's right click is the library's menu.
        QTreeWidgetItem* tail = tree->topLevelItem(2)->child(0);
        tree->scrollToItem(tail);
        emit tree->customContextMenuRequested(tree->visualItemRect(tail).center());
        QCOMPARE(takeMenu(&w), QStringList({"Edit template", "Rename", "Export…", "Delete"}));
        QVERIFY(settle(*w.front()->controller()));
        Settings::setProjectsLocation({});
        useUserDir({});
    }

    // The Explorer follows the editor: the front tab's file is selected, open files are bold, an unsaved one has a dot
    // and the title counts it; a file deleted under its tab stays listed, struck through.
    void theExplorerFollowsTheEditor() {
        QTemporaryDir root, library;
        useUserDir(library.path());
        Settings::setProjectsLocation(root.path());
        MainWindow w(c.get());
        w.show();
        QVERIFY(!addSource(*c, "videotestsrc").isEmpty());
        const QString door = root.filePath("door/door.conf");
        QVERIFY(w.saveFlow()->saveAs(w.front(), door));
        QTreeWidget* tree = w.explorer()->tree();
        auto row = [&](const QString& origin, const QString& path) { return w.explorer()->itemOf(origin + "|" + path); };
        QVERIFY(waitFor([&] { return row("project", door) && tree->currentItem() == row("project", door); }));
        QTreeWidgetItem* file = row("project", door);
        QCOMPARE(w.explorer()->entryOf(file).state, ExplorerEntry::State::Open);
        QVERIFY(file->font(0).bold());
        QCOMPARE(w.panels()->dock(w.explorer())->windowTitle(), QString("Explorer"));

        QVERIFY(!addAfter(*c, fromStd(c->plan().elements.front().name), "src", "fakesink").isEmpty());
        refreshed();
        QCOMPARE(row("project", door)->text(0), QString("door.conf ●"));
        QCOMPARE(w.panels()->dock(w.explorer())->windowTitle(), QString("Explorer · 1 unsaved"));
        QCOMPARE(w.findChild<QAction*>("actionPanelExplorer")->text(), QString("Explorer"));  // View > Panels keeps its name
        QVERIFY(c->undo());
        refreshed();
        QCOMPARE(row("project", door)->text(0), QString("door.conf"));
        QCOMPARE(w.panels()->dock(w.explorer())->windowTitle(), QString("Explorer"));

        // Another tab in front: its file is selected; the first stays bold, it is still open.
        w.newDocument();
        QVERIFY(!addSource(*w.front()->controller(), "videotestsrc").isEmpty());
        const QString yard = root.filePath("yard/yard.conf");
        QVERIFY(w.saveFlow()->saveAs(w.front(), yard));
        QVERIFY(waitFor([&] { return row("project", yard) && tree->currentItem() == row("project", yard); }));
        QVERIFY(row("projects", door)->font(0).bold());

        // Deleted on disk under its tab: listed still, struck through, until it is saved again.
        QVERIFY(QFile::remove(yard));
        QVERIFY(waitFor([&] { return row("project", yard) && w.explorer()->entryOf(row("project", yard)).state == ExplorerEntry::State::Deleted; }));
        QVERIFY(row("project", yard)->font(0).strikeOut());
        QVERIFY(w.saveFlow()->save(w.front()));
        QVERIFY(waitFor([&] { return row("project", yard) && w.explorer()->entryOf(row("project", yard)).state == ExplorerEntry::State::Open; }));
        Settings::setProjectsLocation({});
        useUserDir({});
    }

    // F2 renames in place: each keystroke is checked, Enter refuses a name taken. A renamed conf keeps its tab, which
    // saves to the new file, and the project's manifest lists the new name.
    void renamingAConfInPlaceRebindsItsTab() {
        QTemporaryDir root, library;
        useUserDir(library.path());
        MainWindow w(c.get());
        w.show();
        QVERIFY(!addSource(*c, "videotestsrc").isEmpty());
        const QString door = root.filePath("door/door.conf"), front = root.filePath("door/front.conf");
        QVERIFY(w.saveFlow()->saveAs(w.front(), door));
        QFile taken(root.filePath("door/taken.conf"));
        QVERIFY(taken.open(QIODevice::WriteOnly));
        taken.close();
        QTreeWidget* tree = w.explorer()->tree();
        QVERIFY(waitFor([&] { return w.explorer()->itemOf("project|" + taken.fileName()) != nullptr; }));
        tree->setFocus();
        tree->setCurrentItem(w.explorer()->itemOf("project|" + door));
        QTest::keyClick(tree, Qt::Key_F2);
        auto* editor = tree->findChild<QLineEdit*>("explorerRenameEditor");
        QVERIFY(editor);
        QVERIFY(waitFor([&] { return editor->selectedText() == "door"; }));  // the name without its ending
        QTest::keyClicks(editor, "taken");
        QVERIFY(editor->property("refused").toString().contains("already exists"));
        QTest::keyClick(editor, Qt::Key_Return);
        QVERIFY(tree->findChild<QLineEdit*>("explorerRenameEditor"));  // still open
        editor->selectAll();
        QTest::keyClicks(editor, "front.conf");
        QVERIFY(editor->property("refused").toString().isEmpty());
        QTest::keyClick(editor, Qt::Key_Return);
        QVERIFY(waitFor([&] { return c->target().path() == front; }));
        QVERIFY(!QFile::exists(door) && QFile::exists(front));
        QCOMPARE(w.tabs()->tabText(0), QString("front.conf"));
        QCOMPARE(ProjectFile::load(ProjectFile::pathForConf(front)).confs(), QStringList({"front.conf"}));
        QVERIFY(!c->modified());
        QVERIFY(waitFor([&] { return w.explorer()->itemOf("project|" + front) && tree->currentItem() == w.explorer()->itemOf("project|" + front); }));
        QVERIFY(w.saveFlow()->save(w.front()));
        QVERIFY(QFile::exists(front) && !QFile::exists(door));
        useUserDir({});
    }

    // A library template renamed anywhere: the tab that edits it saves under the new name, and the copies follow.
    void renamingALibraryTemplateRebindsItsTab() {
        QTemporaryDir library;
        useUserDir(library.path());
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        QVERIFY(saveTemplate(*c, "Tail", {convert, sink}));
        QVERIFY(settle(*c));
        const QString id = c->outline().collapsedTemplate(convert);
        Document* tab = w.openTemplate("Tail");
        QVERIFY(waitFor([&] { return settle(*tab->controller()) && tab->controller()->plan().elements.size() == 2; }, 30000));
        support::refreshed();
        QVERIFY(!w.findChild<QAction*>("actionSaveAs")->isEnabled());  // a template is saved to the library, not as a conf
        ExplorerEntry tail;
        tail.kind = ExplorerEntry::Kind::Component;
        tail.origin = TemplateLibrary::kKind;
        tail.path = tail.name = "Tail";
        QVERIFY(!w.fileOperations()->nameError(tail, "").isEmpty());
        QVERIFY(w.fileOperations()->rename(tail, "End"));
        QCOMPARE(tab->controller()->target().libraryTemplate(), QString("End"));
        QCOMPARE(tab->title(), QString("Template: End"));
        QCOMPARE(fromStd(c->plan().instance(toStd(id))->templateName), QString("End"));
        QTest::qWait(4 * DocumentRegistry::kSettleMs);
        QVERIFY(!tab->fileDeleted());  // its old file went with the rename: no deleted mark
        QVERIFY(!w.findChild<QMessageBox*>("changedOnDisk"));
        useUserDir({});
    }

    // A conf not on disk that a tab has open (a new project before its first save, a file deleted under it) opens its
    // tab from the Explorer; one no tab has offers only its path.
    void aConfNotOnDiskOpensItsTabFromTheExplorer() {
        QTemporaryDir root;
        ProjectSection section(c->services().library(), nullptr);
        ExplorerEntry e;
        e.kind = ExplorerEntry::Kind::Conf;
        e.path = root.filePath("fresh/fresh.conf");
        for (auto state : {ExplorerEntry::State::Open, ExplorerEntry::State::Unsaved, ExplorerEntry::State::Deleted}) {
            e.state = state;
            QCOMPARE(section.actions(e), (QList<ExplorerAction>{ExplorerAction::Open, ExplorerAction::CopyPath}));
        }
        e.state = ExplorerEntry::State::Missing;
        QCOMPARE(section.actions(e), QList<ExplorerAction>{ExplorerAction::CopyPath});
    }

    // Del moves to the trash after a confirm, which warns when the file is open with unsaved changes.
    void deleteMovesToTheTrashAfterAConfirm() {
        QTemporaryDir root, library, data;
        useUserDir(library.path());
        qputenv("XDG_DATA_HOME", data.path().toUtf8());  // the trash of this test
        MainWindow w(c.get());
        w.show();
        QVERIFY(!addSource(*c, "videotestsrc").isEmpty());
        const QString door = root.filePath("door/door.conf"), notes = root.filePath("door/notes.txt");
        QVERIFY(w.saveFlow()->saveAs(w.front(), door));
        QFile file(notes);
        QVERIFY(file.open(QIODevice::WriteOnly) && file.write("x") == 1);
        file.close();
        QTreeWidget* tree = w.explorer()->tree();
        QVERIFY(waitFor([&] { return w.explorer()->itemOf("project|" + notes) != nullptr; }));
        tree->setFocus();
        tree->setCurrentItem(w.explorer()->itemOf("project|" + notes));
        QString asked;
        answer(&w, "moveToTrash", &asked);
        QTest::keyClick(tree, Qt::Key_Delete);
        QVERIFY(asked.startsWith("Move notes.txt to the trash?"));
        QVERIFY(!QFile::exists(notes));
        QVERIFY(waitFor([&] { return w.explorer()->itemOf("project|" + notes) == nullptr; }));
        QCOMPARE(c->plan().elements.size(), size_t(1));  // the canvas's Delete did not see the key
        w.scene()->element(fromStd(c->plan().elements.front().name))->setSelected(true);
        support::refreshed();
        tree->setFocus();
        QTest::keyClick(tree, Qt::Key_Backspace);  // nor Backspace, nor the other canvas keys
        QTest::keyClick(tree, Qt::Key_X, Qt::ControlModifier);
        QCOMPARE(c->plan().elements.size(), size_t(1));

        QVERIFY(!addAfter(*c, fromStd(c->plan().elements.front().name), "src", "fakesink").isEmpty());
        tree->setCurrentItem(w.explorer()->itemOf("project|" + door));
        answer(&w, "cancel", &asked);
        QTest::keyClick(tree, Qt::Key_Delete);
        QVERIFY(asked.contains("Open with unsaved changes: door.conf"));
        QVERIFY(QFile::exists(door));
        qunsetenv("XDG_DATA_HOME");
        useUserDir({});
    }

    // Another tab saved a newer revision of the template a tab edits: said as the library's news, not as another
    // program's edit; a clean tab reads it, a modified one is asked once.
    void aTemplateTabHearsOfANewerRevisionFromTheLibrary() {
        QTemporaryDir library;
        useUserDir(library.path());
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        QVERIFY(saveTemplate(*c, "Tail", {convert, sink}));
        QVERIFY(settle(*c));
        Document* tab = w.openTemplate("Tail");
        Controller* t = tab->controller();
        QVERIFY(waitFor([&] { return settle(*t) && t->plan().elements.size() == 2; }, 30000));
        TemplateLibrary& lib = c->services().library();
        auto revise = [&] {
            gps::Graph next = lib.content("Tail");
            next.revision = lib.nextRevision("Tail", next.revision);
            return lib.save("Tail", next).ok;
        };
        QVERIFY(revise());
        QVERIFY(waitFor([&] { return t->plan().revision == 2 && !t->busy(); }));
        QVERIFY(w.statusBar()->currentMessage().contains("has a newer revision from another tab"));
        QVERIFY(!w.findChild<QMessageBox*>("changedOnDisk"));

        QVERIFY(t->editElement(fromStd(t->plan().elements.front().name), withProps({{"qos", "false"}})));
        QVERIFY(t->modified());
        QVERIFY(revise());
        QVERIFY(waitFor([&] { auto* box = w.findChild<QMessageBox*>("changedOnDisk"); return box && box->isVisible(); }));
        auto* box = w.findChild<QMessageBox*>("changedOnDisk");
        QVERIFY(box->text().contains("newer revision, saved from another tab"));
        box->findChild<QPushButton*>("keepMine")->click();
        QTest::qWait(4 * DocumentRegistry::kSettleMs);
        QVERIFY(!w.findChild<QMessageBox*>("changedOnDisk"));  // the watcher does not say it again
        QVERIFY(t->modified());
        useUserDir({});
    }

    // The peek shows the real pipeline: a branch is two rows, not one chain.
    void thePeekLaysOutTheRealPipeline() {
        gps::Graph content;
        content.name = "Split";
        const std::string src = content.addElement("videotestsrc"), tee = content.addElement("tee");
        const std::string a = content.addElement("fakesink"), b = content.addElement("autovideosink");
        content.linkExisting(src, "src", tee, "sink");
        content.linkExisting(tee, "src_%u#0", a, "sink");
        content.linkExisting(tee, "src_%u#1", b, "sink");
        const layout::Node tree = TemplatePeek::tree(content);
        QCOMPARE(tree.children.size(), 4);
        QCOMPARE(tree.edges.size(), 3);
        TemplatePeek peek(content);
        QVERIFY(waitFor([&] { return peek.laidOut(); }, 30000));
        QVERIFY(peek.width() > 300);
        QVERIFY(peek.grab().save(shotDir() + "/template_peek_branch.png"));
    }

    void aCollapsedTemplateShowsOnlyItsParametersAsProperties() {
        QTemporaryDir library;
        useUserDir(library.path());
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        QVERIFY(c->setCaps(convert, "src", "video/x-raw, width=(int)320", {}));
        QVERIFY(settle(*c));
        const QString id = c->beginTemplate({convert, sink});
        std::vector<gps::ExposedParam> chosen;
        for (const gps::ExposedParam& p : c->parameterChoices(c->outline().membersOf(id))) {
            if (p.name == c->names().element(toStd(convert)) + ".caps.width" || p.name == c->names().element(toStd(sink)) + ".sync")
                chosen.push_back(p);
        }
        QCOMPARE(chosen.size(), size_t(2));
        QString error;
        QVERIFY2(c->finishTemplate(id, "Tail", chosen, &error), qPrintable(error));
        c->setTemplateExpanded(id, false);
        QVERIFY(settle(*c));

        w.scene()->frameActivated(id);  // what a click on the collapsed box does
        auto* page = w.propertiesTab()->findChild<TemplateParamsPage*>();
        QVERIFY(page);
        QCOMPARE(w.flows()->propertiesTitle(), QString("Template · Tail"));
        auto* width = page->findChild<QLineEdit*>("param_" + fromStd(c->names().element(toStd(convert))) + ".caps.width");
        QVERIFY(width);
        QCOMPARE(width->text(), QString("320"));
        QVERIFY(page->findChild<QWidget*>("param_" + fromStd(c->names().element(toStd(sink))) + ".sync"));
        QVERIFY(!page->findChild<QWidget*>("param_" + fromStd(c->names().element(toStd(sink))) + ".async"));  // not a parameter: stays inside
        width->clear();
        QTest::keyClicks(width, "640");
        press(page, "saveButton");
        QVERIFY(waitFor([&] { return !c->busy() && c->plan().links.back().caps.find("640") != std::string::npos; }, 8000));
        QVERIFY(text(c->plan().links[size_t(c->plan().linkFrom(toStd(convert), "src"))].caps).contains("width=(int)640"));
        QVERIFY(!gps::templates::modified(c->plan(), toStd(id)));  // parameter values are the copy's own
    }

    void changingATemplateAsksWhatNow() {
        QTemporaryDir library;
        useUserDir(library.path());
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        QVERIFY(saveTemplate(*c, "Tail", {convert, sink}));
        QVERIFY(settle(*c));
        const QString id = c->outline().collapsedTemplate(convert);
        c->setTemplateExpanded(id, true);
        // Answers the question the window asks, with the button named, from inside the dialog's own event loop.
        QString asked;
        auto answer = [&asked](const char* button) {
            auto* poll = new QTimer;
            poll->setInterval(20);
            QObject::connect(poll, &QTimer::timeout, [poll, button, &asked] {
                auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if (!box) return;
                poll->stop();
                poll->deleteLater();
                asked = box->text() + "\n" + box->informativeText();
                box->findChild<QPushButton*>(button)->click();
            });
            poll->start();
        };
        answer("undoTemplateChange");
        QVERIFY(c->editElement(convert, withProps({{"qos", "false"}})));
        QVERIFY(waitFor([&] { return !c->plan().find(toStd(convert))->props.count("qos"); }, 5000));  // Undo took it back
        QVERIFY(asked.contains("'Tail' template changes") && asked.contains("Change: Edited the properties of"));
        QVERIFY(c->plan().instance(toStd(id)));

        answer("detachTemplate");
        QVERIFY(c->editElement(convert, withProps({{"qos", "false"}})));
        QVERIFY(waitFor([&] { return !c->plan().instance(toStd(id)); }, 5000));  // Detach: plain elements with the change
        QCOMPARE(c->plan().find(toStd(convert))->props.at("qos"), std::string("false"));
    }

    void wheelNeverEditsValues() {
        QScrollArea area;
        auto* content = new QWidget; content->setMinimumSize(200, 1200);
        auto* spin = new QSpinBox(content); spin->setRange(0, 100); spin->setValue(20);
        area.setWidget(content); area.resize(250, 200); area.show();
        QTest::qWait(20);
        QEnterEvent enter(QPointF(5, 5), QPointF(5, 5), spin->mapToGlobal(QPoint(5, 5)));
        QApplication::sendEvent(spin, &enter);
        auto wheel = [&] {
            QWheelEvent event(QPointF(5, 5), spin->mapToGlobal(QPoint(5, 5)), QPoint(), QPoint(0, -120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(spin, &event);
        };
        wheel();
        QCOMPARE(spin->value(), 20);
        QVERIFY(area.verticalScrollBar()->value() > 0);  // the form scrolled instead
        QTest::qWait(550);  // not even after a long hover
        wheel();
        QCOMPARE(spin->value(), 20);
    }

    void aboutShowsBuildIdentityAndAnimation() {
        AboutDialog dialog("GStreamer 1.20.3");
        auto* details = dialog.findChild<QLabel*>("buildDetails");
        QVERIFY(details); QVERIFY(details->text().contains(BuildInfo::version()));
        QVERIFY(details->text().contains(BuildInfo::channel()));
        QVERIFY(details->text().contains(QCoreApplication::applicationFilePath()));
        QVERIFY(dialog.findChild<QMovie*>()->isValid());
    }

    void retractingWireCanBeDestroyedDuringAnimation() {
        QGraphicsScene scene;
        QPointer<ConnectionPreview> wire = new ConnectionPreview;
        scene.addItem(wire); wire->setEndpoints({0,0}, {200,100}, true); wire->retract();
        scene.clear(); QTest::qWait(250); QVERIFY(wire.isNull());
    }

    void connectedMovementFollowsAndCommitsTogether() {
        MainWindow window(c.get()); window.show();
        QString src, mid, sink; buildChain(window, &src, &mid, &sink);
        auto* scene = window.scene();
        const QPointF start = scene->element(src)->pos(), before = scene->element(mid)->pos();
        c->services().view().setConnectedMove(true); scene->beginConnectedMove(src);
        scene->element(src)->setPos(start+QPointF(120,60));
        QCOMPARE(scene->element(mid)->pos(), before+QPointF(120,60));
        scene->finishConnectedMove(); scene->elementDragged(src, start+QPointF(120,60));
        c->services().view().setConnectedMove(false);
        QTest::qWait(100);
        QVERIFY(c->canUndo()); QVERIFY(c->undo());
        QTest::qWait(100);
        QCOMPARE(scene->element(mid)->pos(), before);
    }

    void themeUsesTheColoursOfTheSpec() {
        QCOMPARE(Theme::background().name(), QString("#202224"));
        QCOMPARE(Theme::panel().name(), QString("#242424"));
        QCOMPARE(Theme::line().name(), QString("#3a3a3a"));
        QCOMPARE(Theme::text().name(), QString("#e8e8e8"));
        QCOMPARE(Theme::accent().name(), QString("#76b900"));
        QCOMPARE(Theme::accentHover().name(), QString("#8ed000"));
        QCOMPARE(Theme::error().name(), QString("#ff6369"));  // light enough for 4.5:1 text on every surface
        QCOMPARE(Theme::warning().name(), QString("#f5a524"));
        QVERIFY(Theme::styleSheet().contains("#76b900"));
        QVERIFY(!Theme::styleSheet().contains("@"));  // every token was replaced
        QCOMPARE(QApplication::font().family(), QString("DejaVu Sans"));
        QCOMPARE(QApplication::font().pointSize(), 10);
    }

    // Every item of an open combo popup can be reached: each visible row is whole, and the list scrolls for the rest.
    void comboPopupsShowEveryItem() {
        for (int count : {3, 9, 40}) {
            QWidget window;
            window.resize(420, 260);
            auto* combo = new QComboBox(&window);
            for (int i = 0; i < count; ++i) combo->addItem(QString("item %1").arg(i));
            combo->setCurrentIndex(count - 1);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            combo->showPopup();
            QAbstractItemView* list = combo->view();
            QTRY_VERIFY(list->isVisible());
            const QRect inside = list->viewport()->rect();
            int whole = 0;
            for (int i = 0; i < count; ++i) {
                const QRect row = list->visualRect(combo->model()->index(i, 0));
                whole += row.top() >= inside.top() && row.bottom() <= inside.bottom();
            }
            QCOMPARE(whole, qMin(count, combo->maxVisibleItems()));
            combo->hidePopup();
        }
    }

    // ---- PropertyPage

    void propertyPageMakesTheRightEditor() {
        const ElementInfo* info = need(*c, "propdemo");
        PropertyPage d(inputFor(info));
        QVERIFY(in<QCheckBox>(d, "editor_p-bool"));
        auto* spin = in<QSpinBox>(d, "editor_p-int");
        QVERIFY(spin);
        QCOMPARE(spin->minimum(), -10);
        QCOMPARE(spin->maximum(), 100);
        QCOMPARE(spin->value(), 5);
        auto* uspin = in<QDoubleSpinBox>(d, "editor_p-uint");
        QVERIFY(uspin);
        QCOMPARE(uspin->decimals(), 0);
        QCOMPARE(uspin->maximum(), 4294967295.0);
        auto* big = in<QLineEdit>(d, "editor_p-int64");
        QVERIFY(big && big->validator());
        QVERIFY(in<QLineEdit>(d, "editor_p-uint64")->validator());
        auto* real = in<QDoubleSpinBox>(d, "editor_p-float");
        QVERIFY(real);
        QCOMPARE(real->maximum(), 10.0);
        QCOMPARE(real->value(), 0.5);
        auto* combo = in<QComboBox>(d, "editor_p-enum");
        QVERIFY(combo);
        QCOMPARE(combo->count(), 3);
        QCOMPARE(combo->currentData().toString(), QString("one"));
        QWidget* flags = d.findChild<QWidget*>("editor_p-flags");
        QCOMPARE(flags->findChildren<QCheckBox*>().size(), 3);
        QVERIFY(flags->findChildren<QCheckBox*>()[0]->isChecked());   // a
        QVERIFY(!flags->findChildren<QCheckBox*>()[1]->isChecked());  // b
        QCOMPARE(in<QLineEdit>(d, "editor_p-caps")->text(), QString("ANY"));
        QVERIFY(in<QLineEdit>(d, "editor_p-fraction")->validator());
        QCOMPARE(in<QLineEdit>(d, "editor_p-string")->text(), QString("hello"));

        // Read-only and object-like properties are not offered; deprecated ones are dimmed.
        // (gps-inspect never lists GstObject's name and parent, so the GUI has no rule for them.)
        QVERIFY(!d.findChild<QWidget*>("row_p-readonly"));
        QVERIFY(!d.findChild<QWidget*>("row_p-object"));
        QWidget* old = d.findChild<QWidget*>("row_p-old");
        QVERIFY(old);
        QVERIFY(old->findChild<QLabel*>()->text().contains("(deprecated)"));
        QVERIFY(old->findChild<QLabel*>()->styleSheet().contains(Theme::muted().name()));
        QVERIFY(!d.findChild<QWidget*>("row_p-int")->findChild<QLabel*>()->text().contains("(deprecated)"));
    }

    void propertyPageReturnsOnlyWhatChanged() {
        const ElementInfo* info = need(*c, "propdemo");
        PropertyPageInput input = inputFor(info);
        input.props = {{"fake-unknown", "keep-me"}};
        PropertyPage d(input);
        d.show();
        QVERIFY(d.edit().props.size() == 1);  // untouched: only the entry no editor knows

        in<QSpinBox>(d, "editor_p-int")->setValue(42);
        in<QCheckBox>(d, "editor_p-bool")->click();
        in<QComboBox>(d, "editor_p-enum")->setCurrentIndex(1);
        d.findChild<QWidget*>("editor_p-flags")->findChildren<QCheckBox*>()[1]->click();
        in<QDoubleSpinBox>(d, "editor_p-double")->setValue(2.25);
        auto* text = in<QLineEdit>(d, "editor_p-string");
        text->selectAll();
        QTest::keyClicks(text, "world");
        for (const char* name : {"editor_p-uint64", "editor_p-int64"}) in<QLineEdit>(d, name)->clear();
        QTest::keyClicks(in<QLineEdit>(d, "editor_p-uint64"), "18446744073709551615");
        QTest::keyClicks(in<QLineEdit>(d, "editor_p-int64"), "-5");
        QTest::keyClicks(in<QLineEdit>(d, "editor_p-fraction"), "x");  // rejected by the validator
        QSignalSpy confirmed(&d, &PropertyPage::confirmed);
        press(&d, "saveButton");
        QCOMPARE(confirmed.count(), 1);

        ElementEdit e = d.edit();
        QCOMPARE(e.props.at("p-int"), std::string("42"));
        QCOMPARE(e.props.at("p-bool"), std::string("true"));
        QCOMPARE(e.props.at("p-enum"), std::string("two"));
        QCOMPARE(e.props.at("p-flags"), std::string("a+b"));
        QCOMPARE(e.props.at("p-double"), std::string("2.25"));
        QCOMPARE(e.props.at("p-string"), std::string("world"));
        QCOMPARE(e.props.at("p-uint64"), std::string("18446744073709551615"));
        QCOMPARE(e.props.at("p-int64"), std::string("-5"));
        QCOMPARE(e.props.at("fake-unknown"), std::string("keep-me"));
        QVERIFY(e.props.find("p-fraction") == e.props.end());  // the "x" never got in, nothing changed
        QVERIFY(e.props.find("p-uint") == e.props.end());

        // Putting a value back to its default removes it from the plan.
        in<QSpinBox>(d, "editor_p-int")->setValue(5);
        gps::Props back = d.edit().props;
        QVERIFY(back.find("p-int") == back.end());
    }

    void propertyPageStartsFromThePlansValues() {
        const ElementInfo* info = need(*c, "propdemo");
        PropertyPageInput input = inputFor(info);
        input.props = {{"p-int", "77"}, {"p-enum", "3"}, {"p-flags", "b+c"}, {"p-bool", "true"}};
        PropertyPage d(input);
        QCOMPARE(in<QSpinBox>(d, "editor_p-int")->value(), 77);
        QCOMPARE(in<QComboBox>(d, "editor_p-enum")->currentData().toString(), QString("three"));  // number -> nick
        QVERIFY(in<QCheckBox>(d, "editor_p-bool")->isChecked());
        auto boxes = d.findChild<QWidget*>("editor_p-flags")->findChildren<QCheckBox*>();
        QVERIFY(!boxes[0]->isChecked() && boxes[1]->isChecked() && boxes[2]->isChecked());
        QCOMPARE(d.edit().props.at("p-flags"), std::string("b+c"));  // untouched values are kept as written
    }

    void propertyPageSearchHidesRows() {
        PropertyPage d(inputFor(need(*c, "propdemo")));
        d.show();
        auto* search = d.findChild<QLineEdit*>("searchBox");
        QTest::keyClicks(search, "frac");
        QVERIFY(d.findChild<QWidget*>("row_p-fraction")->isVisible());
        QVERIFY(!d.findChild<QWidget*>("row_p-int")->isVisible());
        search->clear();
        QVERIFY(d.findChild<QWidget*>("row_p-int")->isVisible());
        QTest::keyClicks(search, "nothing-like-this");
        QVERIFY(!d.findChild<QWidget*>("row_p-fraction")->isVisible());
    }

    void propertyPageCancelChangesNothing() {
        PropertyPage d(inputFor(need(*c, "propdemo")));
        d.show();
        in<QSpinBox>(d, "editor_p-int")->setValue(50);
        QSignalSpy cancelled(&d, &DrawerPage::cancelled);
        press(&d, "cancelButton");
        QCOMPARE(cancelled.count(), 1);
    }

    void propertyPageRequestSrcPads() {
        const ElementInfo* tee = need(*c, "tee");
        PropertyPageInput input = inputFor(tee);
        input.requestSrc = gps::defaultRequestSrc({{"src_%u", "request"}});  // a new tee: one pad
        PropertyPage fresh(input);
        QCOMPARE(fresh.findChild<QSpinBox*>("padCount_src_%u")->value(), 1);
        QCOMPARE(fresh.findChild<QSpinBox*>("padCount_src_%u")->minimum(), 0);

        input.requestSrc = {{"src_%u", 3}};
        input.linkedSrc = {{"src_%u", 2}};
        PropertyPage d(input);
        auto* pads = d.findChild<QSpinBox*>("padCount_src_%u");
        QCOMPARE(pads->value(), 3);
        QCOMPARE(pads->minimum(), 2);  // cannot go below the linked pads
        pads->setValue(4);
        QCOMPARE(d.edit().requestSrc.at("src_%u"), 4);

        // An element with only Always pads has no such section.
        PropertyPage plain(inputFor(need(*c, "queue")));
        QVERIFY(plain.findChildren<QSpinBox*>().isEmpty());
    }

    // ---- CapsPage

    void capsPageBuildsTheChoice() {
        CapsPageInput input;
        const ElementInfo* src = need(*c, "videotestsrc");
        input.allowed = src->padTemplate("src", "src")->structures;
        CapsPage d(input);
        d.show();
        QCOMPARE(d.caps(), QString());  // Auto
        auto* media = d.findChild<QComboBox*>("mediaCombo");
        QCOMPARE(media->count(), 2);
        QCOMPARE(media->itemText(1), QString("video/x-raw"));
        media->setCurrentIndex(1);
        QCOMPARE(d.caps(), QString("video/x-raw"));  // every field still Auto

        // list -> combo box with Auto first
        auto* format = d.findChild<QWidget*>("field_format")->findChild<QComboBox*>();
        QCOMPARE(format->itemText(0), QString("Auto"));
        QCOMPARE(format->count(), 4);
        format->setCurrentIndex(2);
        QCOMPARE(d.caps(), QString("video/x-raw, format=(string)NV12"));

        // range -> number box, active once "Pin" is ticked
        QWidget* width = d.findChild<QWidget*>("field_width");
        auto* pin = width->findChild<QCheckBox*>();
        auto* number = width->findChild<QSpinBox*>();
        QVERIFY(!number->isEnabled());
        pin->click();
        QVERIFY(number->isEnabled());
        number->setValue(640);
        QCOMPARE(d.caps(), QString("video/x-raw, format=(string)NV12, width=(int)640"));
        auto* rate = d.findChild<QWidget*>("field_framerate");
        rate->findChild<QCheckBox*>()->click();
        rate->findChild<QLineEdit*>()->setText("15/1");
        QCOMPARE(d.caps(), QString("video/x-raw, format=(string)NV12, width=(int)640, framerate=(fraction)15/1"));
        pin->click();  // unticked: Auto again
        QCOMPARE(d.caps(), QString("video/x-raw, format=(string)NV12, framerate=(fraction)15/1"));
        media->setCurrentIndex(0);
        QCOMPARE(d.caps(), QString());
    }

    void capsPageFixedFieldsAndFeatures() {
        const ElementInfo* dec = need(*c, "jpegdec");
        CapsPageInput input;
        input.allowed = dec->padTemplate("sink", "sink")->structures;
        CapsPage d(input);
        d.findChild<QComboBox*>("mediaCombo")->setCurrentIndex(1);
        QWidget* parsed = d.findChild<QWidget*>("field_parsed");
        QVERIFY(parsed->findChild<QLabel*>());  // fixed: shown as a label
        QCOMPARE(parsed->findChild<QLabel*>()->text(), QString("true"));
        parsed->findChild<QCheckBox*>()->click();
        QCOMPARE(d.caps(), QString("image/jpeg, parsed=(boolean)true"));

        const ElementInfo* nv = need(*c, "nvvideoconvert");
        CapsPageInput nvInput;
        nvInput.allowed = nv->padTemplate("src", "src")->structures;
        CapsPage n(nvInput);
        auto* media = n.findChild<QComboBox*>("mediaCombo");
        QCOMPARE(media->itemText(1), QString("video/x-raw(memory:NVMM)"));
        media->setCurrentIndex(1);
        QCOMPARE(n.caps(), QString("video/x-raw(memory:NVMM)"));
    }

    void capsPagePadSettings() {
        const ElementInfo* mixer = need(*c, "compositor");
        CapsPageInput input;
        input.allowed = mixer->padTemplate("src", "src")->structures;
        input.padProperties = mixer->padTemplate("sink_%u", "sink")->padProperties;
        input.padProps = {{"xpos", "100"}};
        CapsPage d(input);
        d.show();
        QVERIFY(d.findChild<QWidget*>("padSettings")->isVisible());
        QCOMPARE(d.findChild<QWidget*>("padEditor_xpos")->findChild<QSpinBox*>()->value(), 100);
        d.findChild<QWidget*>("padEditor_ypos")->findChild<QSpinBox*>()->setValue(-20);
        QCOMPARE(d.padProps().at("xpos"), std::string("100"));
        QCOMPARE(d.padProps().at("ypos"), std::string("-20"));

        // No pad properties: the section is not there.
        CapsPageInput plain;
        plain.allowed = input.allowed;
        CapsPage p(plain);
        p.show();
        QVERIFY(!p.findChild<QWidget*>("padSettings")->isVisible());
    }

    void capsPagePrefillsAndHandlesNothingKnown() {
        const ElementInfo* src = need(*c, "videotestsrc");
        CapsPageInput input;
        input.allowed = src->padTemplate("src", "src")->structures;
        input.current = "video/x-raw, format=(string)RGBA, width=(int)800";
        CapsPage d(input);
        QCOMPARE(d.findChild<QComboBox*>("mediaCombo")->currentIndex(), 1);
        QCOMPARE(d.caps(), QString("video/x-raw, format=(string)RGBA, width=(int)800"));

        CapsPageInput empty;  // an update gave no structures
        CapsPage e(empty);
        e.show();
        QCOMPARE(e.findChild<QComboBox*>("mediaCombo")->count(), 1);
        QCOMPARE(e.caps(), QString());
        QSignalSpy applied(&e, &CapsPage::applied);
        press(&e, "saveButton");
        QCOMPARE(applied.count(), 1);
    }

    // ---- PickerPage

    void pickerRanksAndSearches() {
        PickerData data;
        data.existing = {{"mix_1", "compositor", "sink_%u", true}, {"over_2", "textoverlay", "video_sink", false}};
        data.compatible = {"jpegdec", "videoconvert"};
        for (const FactoryEntry& e : c->services().catalog().entries()) {
            if (!e.isSource) data.candidates.push_back(e);
        }
        Drawer drawer;  // Enter and Esc are the drawer's
        drawer.show();
        auto* d = new PickerPage(data);
        drawer.present(d, "Add after x · src", "video/x-raw", Drawer::Replace);
        auto* list = d->findChild<QListWidget*>("pickerList");
        auto rows = [&] {
            QStringList out;
            for (int i = 0; i < list->count(); ++i) {
                if (!list->item(i)->isHidden()) out << list->item(i)->text();
            }
            return out;
        };
        QStringList all = rows();
        QCOMPARE(all[0], QString("Elements in the pipeline"));
        QCOMPARE(all[1], QString("mix_1 · sink_%u"));
        QCOMPARE(list->item(1)->data(HintRole).toString(), QString("Request a pad"));
        QCOMPARE(all[3], QString("New element"));
        QCOMPARE(all[4], QString("jpegdec"));       // compatible first, in the helper's order
        QCOMPARE(all[5], QString("videoconvert"));
        QCOMPARE(list->item(4)->data(HintRole).toString(), QString("compatible"));
        QVERIFY(list->item(6)->data(HintRole).toString().isEmpty());
        QVERIFY(!all.contains("videotestsrc"));     // sources are never offered behind a pad
        // the rest by rank (256 first), then name
        QCOMPARE(all[6], QString("compositor"));
        QVERIFY(list->currentItem() == list->item(1));

        auto* search = d->findChild<QLineEdit*>("searchBox");
        QCOMPARE(search->window()->focusWidget(), search);  // first focus: the search box
        QTest::keyClicks(search, "tee");
        QCOMPARE(rows(), QStringList({"New element", "tee"}));  // the empty section header goes too
        QCOMPARE(d->choice().kind, PickerChoice::NewElement);
        QCOMPARE(d->choice().factory, QString("tee"));
        search->clear();
        QTest::keyClicks(search, "request");
        QCOMPARE(rows(), QStringList({"Elements in the pipeline", "mix_1 · sink_%u"}));
        QCOMPARE(d->choice().kind, PickerChoice::Existing);
        QCOMPARE(d->choice().existing.element, QString("mix_1"));
        QVERIFY(d->choice().existing.request);
        search->clear();
        QTest::keyClicks(search, "zzzz");
        QVERIFY(rows().isEmpty());
        QCOMPARE(d->choice().kind, PickerChoice::None);

        // Enter takes the best hit; Down moves on.
        search->clear();
        QTest::keyClicks(search, "con");
        QTest::keyClick(search, Qt::Key_Down);
        QCOMPARE(d->choice().factory, QString("nvvideoconvert"));
        QTest::keyClick(search, Qt::Key_Up);
        QCOMPARE(d->choice().factory, QString("videoconvert"));
        QSignalSpy picked(d, &PickerPage::picked);
        QTest::keyClick(search, Qt::Key_Return);
        QCOMPARE(picked.count(), 1);
        QCOMPARE(picked[0][0].value<PickerChoice>().factory, QString("videoconvert"));
    }

    void pickerDoubleClickAndCancel() {
        PickerData data;
        data.compatible = {"queue"};
        for (const FactoryEntry& e : c->services().catalog().entries()) {
            if (!e.isSource) data.candidates.push_back(e);
        }
        PickerPage first(data);
        first.show();
        QSignalSpy cancelled(&first, &DrawerPage::cancelled);
        press(&first, "cancelButton");
        QCOMPARE(cancelled.count(), 1);

        PickerPage second(data);
        second.show();
        QSignalSpy picked(&second, &PickerPage::picked);
        auto* list = second.findChild<QListWidget*>("pickerList");
        QListWidgetItem* item = list->item(1);
        doubleClick(list->viewport(), list->visualItemRect(item).center());
        QCOMPARE(picked.count(), 1);
        QCOMPARE(second.choice().factory, QString("queue"));
    }

    // ---- canvas

    void canvasDrawsElementsPadsAndLinks() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        CanvasScene* scene = w.scene();
        QVERIFY(scene->element(src) && scene->element(convert) && scene->element(sink));
        QCOMPARE(scene->links().size(), 2);
        QCOMPARE(scene->element(convert)->pos(), QPointF(c->plan().find(convert.toStdString())->x, c->plan().find(convert.toStdString())->y));
        QCOMPARE(scene->pad(src, "src", true)->view().state, PadView::Linked);
        QCOMPARE(scene->pad(convert, "sink", false)->view().state, PadView::Linked);
        QVERIFY(!scene->pad(sink, "src", true));

        // Links run from the edge of one pad to the edge of the other.
        LinkItem* link = scene->links()[0];
        QVERIFY(link->shape().contains(link->mapFromScene(scene->pad(src, "src", true)->center() + QPointF(9, 0))));
        QVERIFY(link->shape().contains(link->mapFromScene(scene->pad(convert, "sink", false)->center() - QPointF(9, 0))));
        QVERIFY(!link->shape().contains(link->mapFromScene(scene->pad(src, "src", true)->center() - QPointF(9, 0))));

        // A caps choice gives the link a badge.
        QVERIFY(c->setCaps(src, "src", "video/x-raw, width=(int)640", {}));
        QVERIFY(settle(*c));
        scene->rebuild();
        QCOMPARE(scene->links()[0]->view().badge, QString("caps · 1"));
        QCOMPARE(scene->links()[0]->toolTip(), QString("video/x-raw, width=(int)640"));
        QVERIFY(scene->links()[0]->shape().contains(scene->links()[0]->midPoint() + QPointF(0, 8)));  // the badge is part of the link
    }

    void canvasShowsRequestSinkPadsOnlyWhenLinked() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString mix = addAfter(*c, src, "src", "compositor", "sink_%u");
        scene_rebuild(w);
        QVERIFY(w.scene()->pad(mix, "sink_%u#0", false));
        QVERIFY(!w.scene()->pad(mix, "sink_%u#1", false));
        QString second = addSource(*c, "videotestsrc");
        need(*c, "compositor");
        QVERIFY(c->linkExisting(second, "src", mix, "sink_%u"));
        QVERIFY(settle(*c));
        scene_rebuild(w);
        QVERIFY(w.scene()->pad(mix, "sink_%u#1", false));
        QCOMPARE(w.scene()->links().size(), 2);
        QCOMPARE(fromStd(w.scene()->links()[1]->view().sink.identity), QString("sink_%u#1"));
    }

    void canvasPadColoursFollowTheSpec() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString mix = addAfter(*c, src, "src", "compositor", "sink_%u");
        QString raw = addSource(*c, "filesrc", withProps({{"location", "/data/random.bin"}}));
        scene_rebuild(w);

        CanvasScene* scene = w.scene();
        QRectF area = scene->itemsBoundingRect().adjusted(-20, -20, 20, 20);
        QImage image(area.size().toSize(), QImage::Format_ARGB32);
        image.fill(Qt::black);
        QPainter painter(&image);
        scene->render(&painter, QRectF(QPointF(0, 0), area.size()), area);
        painter.end();
        auto at = [&](PadItem* pad, double dx) {
            QPointF p = pad->center() - area.topLeft() + QPointF(dx, 0);
            return image.pixelColor(p.toPoint());
        };
        auto near = [](const QColor& a, const QColor& b) {
            return qAbs(a.red() - b.red()) < 24 && qAbs(a.green() - b.green()) < 24 && qAbs(a.blue() - b.blue()) < 24;
        };
        PadItem* linkedSrc = scene->pad(src, "src", true);
        PadItem* freeSrc = scene->pad(mix, "src", true);
        PadItem* unknownSrc = scene->pad(raw, "src", true);
        PadItem* linkedSink = scene->pad(mix, "sink_%u#0", false);
        QVERIFY(linkedSrc && freeSrc && unknownSrc && linkedSink);
        QCOMPARE(freeSrc->view().state, PadView::Free);
        QCOMPARE(unknownSrc->view().state, PadView::Unknown);

        QVERIFY2(near(at(linkedSrc, 0), Theme::accent()), "linked src: filled green");
        QVERIFY2(near(at(freeSrc, 6), Theme::accent()), "free src: green ring");
        QVERIFY2(near(at(freeSrc, 0), Theme::background()), "free src: hollow");
        QVERIFY2(near(at(unknownSrc, 6), Theme::warning()), "unknown data: orange ring");
        QVERIFY2(near(at(linkedSink, 0), Theme::muted()), "linked sink: filled gray");
    }

    void canvasErrorShowsRedOnTheElement() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString sink = addAfter(*c, src, "src", "fakesink", "sink", withProps({{"fake-error", "no way"}}));
        scene_rebuild(w);
        QCOMPARE(w.scene()->element(sink)->view().note, QString("no way"));
        QVERIFY(w.scene()->element(sink)->view().isError);
        QVERIFY(w.scene()->element(src)->view().note.isEmpty());

        QRectF area = w.scene()->itemsBoundingRect().adjusted(-20, -20, 20, 20);
        QImage image(area.size().toSize(), QImage::Format_ARGB32);
        image.fill(Qt::black);
        QPainter painter(&image);
        w.scene()->render(&painter, QRectF(QPointF(0, 0), area.size()), area);
        painter.end();
        QPointF top = w.scene()->element(sink)->pos() + QPointF(120, 0) - area.topLeft();
        QColor border = image.pixelColor(top.toPoint());
        QVERIFY2(qAbs(border.red() - Theme::error().red()) < 30 && border.green() < 110, qPrintable(border.name()));
    }

    void canvasClicksReachTheRightSignals() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        // The last element has no free pad, so make one: cut the tail off.
        QVERIFY(c->cut(convert, "src"));
        QVERIFY(settle(*c));
        w.scene()->rebuild();
        w.view()->fitAll();
        CanvasView* view = w.view();
        QWidget* port = view->viewport();
        // Queued connections would open dialogs: look at the scene's own signals instead.
        QSignalSpy freePad(w.scene(), &CanvasScene::freeSrcPadClicked);
        QSignalSpy linkMenu(w.scene(), &CanvasScene::linkMenuRequested);
        QSignalSpy elementMenu(w.scene(), &CanvasScene::elementMenuRequested);
        QSignalSpy activated(w.scene(), &CanvasScene::elementActivated);
        w.scene()->disconnect(&w);  // keep the window's slots out of this test

        // Left click on the free src pad of the convert element.
        QTest::mouseClick(port, Qt::LeftButton, Qt::NoModifier, viewPoint(view, w.scene()->pad(convert, "src", true)->center()));
        QCOMPARE(freePad.count(), 1);
        QCOMPARE(freePad[0][0].toString(), convert);
        QCOMPARE(freePad[0][1].toString(), QString("src"));
        // A left click on a linked pad does nothing (the menu is on the right button).
        QTest::mouseClick(port, Qt::LeftButton, Qt::NoModifier, viewPoint(view, w.scene()->pad(src, "src", true)->center()));
        QCOMPARE(freePad.count(), 1);
        QCOMPARE(linkMenu.count(), 0);

        // Right click on the linked src pad, on the sink pad it feeds, and on the link itself.
        QTest::mouseClick(port, Qt::RightButton, Qt::NoModifier, viewPoint(view, w.scene()->pad(src, "src", true)->center()));
        QCOMPARE(linkMenu.count(), 1);
        QCOMPARE(linkMenu[0][0].toString(), src);
        QCOMPARE(linkMenu[0][1].toString(), QString("src"));
        QTest::mouseClick(port, Qt::RightButton, Qt::NoModifier, viewPoint(view, w.scene()->pad(convert, "sink", false)->center()));
        QCOMPARE(linkMenu.count(), 2);
        QCOMPARE(linkMenu[1][0].toString(), src);
        LinkItem* link = w.scene()->links()[0];
        QTest::mouseClick(port, Qt::RightButton, Qt::NoModifier, viewPoint(view, link->mapToScene(link->midPoint())));
        QCOMPARE(linkMenu.count(), 3);
        QCOMPARE(linkMenu[2][0].toString(), src);
        // A right click on a free pad is not a link menu.
        QTest::mouseClick(port, Qt::RightButton, Qt::NoModifier, viewPoint(view, w.scene()->pad(convert, "src", true)->center()));
        QCOMPARE(linkMenu.count(), 3);

        // Right click and double click on an element body.
        QPointF body = w.scene()->element(convert)->pos() + QPointF(120, 12);
        QTest::mouseClick(port, Qt::RightButton, Qt::NoModifier, viewPoint(view, body));
        QCOMPARE(elementMenu.count(), 1);
        QCOMPARE(elementMenu[0][0].toString(), convert);
        QTest::mouseDClick(port, Qt::LeftButton, Qt::NoModifier, viewPoint(view, body));
        QVERIFY(activated.count() >= 1);
        QCOMPARE(activated[0][0].toString(), convert);
    }

    void canvasDragSavesThePosition() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        w.scene()->rebuild();
        w.view()->resetTransform();
        w.view()->centerOn(w.scene()->element(src));
        QPointF before = w.scene()->element(src)->pos();
        QPoint from = viewPoint(w.view(), before + QPointF(100, 12));
        QTest::mousePress(w.view()->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QMouseEvent move(QEvent::MouseMove, QPointF(from + QPoint(60, 90)), w.view()->viewport()->mapToGlobal(QPointF(from + QPoint(60, 90))),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(w.view()->viewport(), &move);
        QTest::mouseRelease(w.view()->viewport(), Qt::LeftButton, Qt::NoModifier, from + QPoint(60, 90));
        QVERIFY(waitFor([&] { return c->plan().find(src.toStdString())->x != before.x(); }, 3000));
        QCOMPARE(c->plan().find(src.toStdString())->x, before.x() + 60);
        QCOMPARE(c->plan().find(src.toStdString())->y, before.y() + 90);
        QVERIFY(c->modified());
    }

    void canvasZoomAndPan() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        w.scene()->rebuild();
        CanvasView* view = w.view();
        view->resetTransform();
        double start = view->zoom();
        QWheelEvent in(QPointF(100, 100), QPointF(100, 100), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view->viewport(), &in);
        QVERIFY(view->zoom() > start);
        QWheelEvent out(QPointF(100, 100), QPointF(100, 100), QPoint(), QPoint(0, -240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view->viewport(), &out);
        QVERIFY(view->zoom() < start);
        for (int i = 0; i < 40; ++i) view->zoomBy(1.5);
        QCOMPARE(view->zoom(), 3.0);
        for (int i = 0; i < 80; ++i) view->zoomBy(0.5);
        QCOMPARE(view->zoom(), 0.2);

        // Pan: middle-button drag on the empty background pans; left-button drag rubber-band selects.
        view->resetTransform();
        view->zoomBy(2.0);
        view->centerOn(w.scene()->element(src));
        int before = view->horizontalScrollBar()->value();
        QPoint empty(view->viewport()->width() - 40, view->viewport()->height() - 40);
        QTest::mousePress(view->viewport(), Qt::MiddleButton, Qt::NoModifier, empty);
        QMouseEvent move(QEvent::MouseMove, QPointF(empty - QPoint(80, 0)), view->viewport()->mapToGlobal(QPointF(empty - QPoint(80, 0))),
                         Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &move);
        QTest::mouseRelease(view->viewport(), Qt::MiddleButton, Qt::NoModifier, empty - QPoint(80, 0));
        QVERIFY(view->horizontalScrollBar()->value() != before);

        view->fitAll();
        QVERIFY(view->zoom() <= 1.0);
    }

    // ---- main window and the drawer flows

    void actionsFollowTheStateOfTheUpdate() {
        MainWindow w(c.get());
        w.show();
        auto act = [&](const char* name) { return w.findChild<QAction*>(name); };
        auto label = [&] { return w.findChild<QLabel*>("stateLabel")->text(); };
        // empty canvas
        refreshed();
        QVERIFY(act("actionNew")->isEnabled() && act("actionOpen")->isEnabled() && act("actionAddElement")->isEnabled());
        QVERIFY(act("actionSave")->isEnabled() && !act("actionRun")->isEnabled() && !act("actionUpdate")->isEnabled());
        QVERIFY(label().contains("Empty canvas"));
        QVERIFY(act("actionRun")->toolTip().contains("The canvas is empty"));  // a disabled action says why

        // updating: canvas edits stay available, and New and Open open another tab
        need(*c, "videotestsrc");
        QVERIFY(c->addElement("videotestsrc", {}));
        refreshed();
        QVERIFY(act("actionNew")->isEnabled() && act("actionOpen")->isEnabled() && act("actionAddElement")->isEnabled());
        QVERIFY(act("actionSave")->isEnabled() && !act("actionRun")->isEnabled());
        QVERIFY(label().contains("keep editing"));
        QVERIFY(act("actionRun")->toolTip().contains("Validation is running"));
        QVERIFY(settle(*c));

        // ok but incomplete: Save yes, Run no
        QVERIFY(act("actionSave")->isEnabled());
        QVERIFY(act("actionSaveAs")->isEnabled());
        QVERIFY(!act("actionRun")->isEnabled());
        QVERIFY(label().contains("not complete"));
        QVERIFY(act("actionRun")->toolTip().contains("link every free src pad"));

        // complete: both
        QString src = QString::fromStdString(c->plan().elements[0].name);
        QString sink = addAfter(*c, src, "src", "fakesink");
        QVERIFY(act("actionRun")->isEnabled());
        QCOMPARE(label(), QString("Pipeline is complete"));

        // failed: Run off, red text; Save still writes
        QVERIFY(c->editElement(sink, withProps({{"fake-error", "bad"}})));
        QVERIFY(settle(*c));
        QVERIFY(act("actionSave")->isEnabled() && act("actionSaveAs")->isEnabled() && !act("actionRun")->isEnabled());
        QVERIFY(act("actionUpdate")->isEnabled());  // retrying is allowed
        QVERIFY(label().contains("reported a problem"));
        QVERIFY(w.findChild<QLabel*>("stateLabel")->styleSheet().contains(Theme::error().name()));
        QVERIFY(w.windowTitle().endsWith("*"));
    }

    void statusBarShowsTheLaunchCommandAfterSaving() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString sink = addAfter(*c, src, "src", "fakesink");
        QCOMPARE(w.findChild<QLabel*>("commandLabel")->text(), QString());
        QTemporaryDir dir;
        QVERIFY(w.saveFlow()->saveAs(w.front(), dir.filePath("demo.conf")));
        refreshed();
        const QString command = w.findChild<QLabel*>("commandLabel")->text();
        QVERIFY(command.contains("gst-launch-1.0 demo"));
        QVERIFY(command.contains("GPS_PIPELINE_PATH="));
        QVERIFY(w.windowTitle().contains("demo.conf"));
        QVERIFY(!w.windowTitle().endsWith("*"));
        QVERIFY(w.saveFlow()->save(w.front()));  // Save again, same file
    }

    void updateAndLogPanelsShowTheHelpersAnswer() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString sink = addAfter(*c, src, "src", "fakesink", "sink", withProps({{"fake-error", "kaput"}}));
        UpdatePanel* panel = w.updatePanel();
        QVERIFY(panel->findChild<QLabel*>("updateStatus")->text().startsWith("Problem"));
        QVERIFY(panel->findChild<QLabel*>("updateMessage")->text().contains(sink));  // the element is named
        QVERIFY(panel->findChild<QLabel*>("updateMessage")->text().contains("kaput"));
        QVERIFY(panel->findChild<QLabel*>("updateStatus")->styleSheet().contains(Theme::error().name()));
        QVERIFY(panel->findChild<QPushButton*>("supportButton")->isVisibleTo(&w));  // an update failed: the report is offered

        QVERIFY(c->editElement(sink, {}));
        QVERIFY(settle(*c));
        QVERIFY(panel->findChild<QLabel*>("updateStatus")->text().startsWith("Valid, pipeline complete"));
        QVERIFY(!panel->findChild<QPushButton*>("supportButton")->isVisibleTo(&w));
        QCOMPARE(panel->table()->rowCount(), c->result().pads.size());
        QCOMPARE(panel->table()->rowCount(), 2);  // the src pad and the sink pad
        QCOMPARE(panel->table()->item(0, 0)->text(), src);

        QString log = w.logPanel()->toPlainText();
        QVERIFY(log.contains("[gps-update] fake gps-update: 2 elements, 1 links"));
        QVERIFY(log.contains("Validation: Problem"));
        QVERIFY(log.contains("kaput"));
        QVERIFY(w.logPanel()->isReadOnly());
        QVERIFY(w.logPanel()->font().styleHint() == QFont::Monospace || w.logPanel()->font().family().contains("Mono"));
        QCOMPARE(w.panels()->dock(w.logPanel())->windowTitle(), QString("Log"));
        QCOMPARE(w.panels()->dock(w.updatePanel())->windowTitle(), QString("Validation"));
        // The panels are docks the user moves, closes and brings back from View > Panels; Reset layout tabs them again.
        QAction* logToggle = w.findChild<QAction*>("actionPanelLog");
        QVERIFY(logToggle && logToggle->isCheckable());
        w.panels()->dock(w.logPanel())->setFloating(true);
        w.panels()->dock(w.logPanel())->close();
        QVERIFY(!logToggle->isChecked());
        w.findChild<QAction*>("actionResetLayout")->trigger();
        QVERIFY(logToggle->isChecked() && !w.panels()->dock(w.logPanel())->isFloating());
        QVERIFY(w.panels()->showing(w.updatePanel()));

    }

    void refusedAndUnknownShowInTheUpdatePanel() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        addAfter(*c, src, "src", "jpegdec");
        QVERIFY(w.updatePanel()->findChild<QLabel*>("updateLists")->text().contains("Refused links"));
        QVERIFY(w.updatePanel()->findChild<QLabel*>("updateStatus")->styleSheet().contains(Theme::warning().name()));
    }

    void theDrawerIsClosedAtFirstAndThereIsNoPermanentSourcePanel() {
        MainWindow w(c.get());
        w.show();
        QVERIFY(!w.drawer()->isOpen());
        QCOMPARE(w.drawer()->width(), 0);
        QVERIFY(!w.findChild<QWidget*>("sourcePanel"));
        QVERIFY(w.view()->width() > 900);  // the canvas has the whole window
    }

    void toolbarAddElementOpensThePickerAndTheFlowAddsASource() {
        MainWindow w(c.get());
        w.show();
        w.findChild<QAction*>("actionAddElement")->trigger();
        auto* picker = pageOf<PickerPage>(w);
        QVERIFY(picker);
        QVERIFY(w.drawer()->isOpen());
        QCOMPARE(w.drawer()->title(), QString("Add element"));
        QVERIFY(waitFor([&] { return w.drawer()->width() == Drawer::kWidth; }, 2000));  // the slide ends
        auto* list = picker->findChild<QListWidget*>("pickerList");
        QCOMPARE(list->item(0)->text(), QString("Elements"));
        QStringList names;
        for (int i = 1; i < list->count(); ++i) names << list->item(i)->text();
        QVERIFY(names.contains("videotestsrc") && names.contains("filesrc") && names.contains("nvarguscamerasrc"));
        QVERIFY(names.contains("queue") && names.contains("fakesink"));  // one list: the SRC/FILTER/SINK chips narrow it
        QVERIFY(list->item(names.indexOf("nvarguscamerasrc") + 1)->data(NvidiaRole).toBool());

        // Cancel on the picker: nothing happens and the drawer closes.
        press(picker, "cancelButton");
        QVERIFY(!w.drawer()->isOpen());
        QVERIFY(c->plan().empty());
        QVERIFY(!c->modified());

        // Picker (in right drawer) -> properties (in bottom Properties tab) -> Add element.
        w.findChild<QAction*>("actionAddElement")->trigger();
        picker = pageOf<PickerPage>(w);
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "audiot");
        QTest::keyClick(picker->findChild<QLineEdit*>("searchBox"), Qt::Key_Return);
        auto* props = pageOf<PropertyPage>(w);
        QVERIFY(props);
        QVERIFY(!w.drawer()->isOpen());  // the right drawer is exclusively for picking elements
        QVERIFY(w.panels()->showing(w.propertiesTab()));
        QCOMPARE(w.flows()->propertiesTitle(), QString("Add element · audiotestsrc"));
        QCOMPARE(props->primary()->text(), QString("Add element"));
        in<QSpinBox>(*props, "editor_num-buffers")->setValue(25);
        press(props, "saveButton");
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 1 && !c->busy(); }, 8000));
        QCOMPARE(c->plan().elements[0].factory, std::string("audiotestsrc"));
        QCOMPARE(c->plan().elements[0].props.at("num-buffers"), std::string("25"));
        QVERIFY(!w.drawer()->isOpen());
        QVERIFY(!w.flows()->propertyPage());
        QVERIFY(waitFor([&] { return w.scene()->element("audiotestsrc_0") != nullptr; }));
    }

    void escAndEnterWorkInTheDrawer() {
        MainWindow w(c.get());
        w.show();
        w.flows()->chooseElement();
        auto* picker = pageOf<PickerPage>(w);
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "videot");
        QTest::keyClick(picker->findChild<QLineEdit*>("searchBox"), Qt::Key_Escape);
        QVERIFY(!w.drawer()->isOpen());
        QVERIFY(c->plan().empty());
        // Ctrl+F puts the cursor in the search box of the picker.
        w.flows()->chooseElement();
        picker = pageOf<PickerPage>(w);
        picker->findChild<QListWidget*>("pickerList")->setFocus();
        w.findChild<QAction*>("actionFind")->trigger();
        QCOMPARE(picker->window()->focusWidget(), picker->findChild<QLineEdit*>("searchBox"));
        w.drawer()->dismiss();
    }

    void addElementBehindAPadWithEveryCancelPoint() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        w.scene()->rebuild();
        w.view()->fitAll();
        auto clickFreePad = [&](const QString& element) {
            w.scene()->rebuild();
            QTest::mouseClick(w.view()->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint(w.view(), w.scene()->pad(element, "src", true)->center()));
        };
        QString before = text(c->plan().toConf());

        // 1. Picker: the drawer opens with the pad in its title. Cancel.
        clickFreePad(src);
        auto* picker = pageOf<PickerPage>(w);
        QVERIFY(picker);
        QCOMPARE(w.drawer()->title(), "Add after " + c->padName({toStd(src), "src", gps::Pad::Direction::Source}));
        QCOMPARE(w.drawer()->subtitle(), QString("video/x-raw"));
        QCOMPARE(picker->findChild<QListWidget*>("pickerList")->item(0)->text(), QString("New element"));  // nothing existing yet
        press(picker, "cancelButton");
        QVERIFY(!w.drawer()->isOpen());
        QCOMPARE(text(c->plan().toConf()), before);

        // 2. Properties page (in bottom Properties tab): Cancel.
        clickFreePad(src);
        picker = pageOf<PickerPage>(w);
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "videoconvert");
        press(picker, "saveButton");
        auto* props = pageOf<PropertyPage>(w);
        QVERIFY(props);
        QVERIFY(!w.drawer()->isOpen());
        QVERIFY(w.panels()->showing(w.propertiesTab()));
        QVERIFY(w.flows()->propertiesTitle().contains("videoconvert"));
        press(props, "cancelButton");
        QVERIFY(!w.flows()->propertyPage());
        QCOMPARE(text(c->plan().toConf()), before);

        // 3. Add element, then the caps page (in bottom Caps tab): Cancel leaves the element with Auto caps.
        clickFreePad(src);
        picker = pageOf<PickerPage>(w);
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "videoconvert");
        press(picker, "saveButton");
        props = pageOf<PropertyPage>(w);
        press(props, "saveButton");
        auto* caps = pageOf<CapsPage>(w);
        QVERIFY(caps);
        QVERIFY(!c->busy());
        QCOMPARE(c->plan().elements.size(), size_t(2));
        QVERIFY(!w.drawer()->isOpen());
        QVERIFY(w.panels()->showing(w.capsTab()));
        QCOMPARE(caps->findChild<QComboBox*>("mediaCombo")->itemText(1), QString("video/x-raw"));
        press(caps, "cancelButton");
        QVERIFY(!w.flows()->capsPage());
        QCOMPARE(c->plan().elements.size(), size_t(2));
        QVERIFY(c->plan().links[0].caps.empty());
        QString convert = text(c->plan().elements[1].name);

        // 4. Skip keeps Auto too.
        clickFreePad(convert);
        picker = pageOf<PickerPage>(w);
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "queue");
        QTest::keyClick(picker->findChild<QLineEdit*>("searchBox"), Qt::Key_Return);
        press(pageOf<PropertyPage>(w), "saveButton");
        caps = pageOf<CapsPage>(w);
        QVERIFY(caps);
        press(caps, "skipButton");
        QVERIFY(!w.flows()->capsPage());
        QVERIFY(c->plan().links[1].caps.empty());
        QString queue = text(c->plan().elements[2].name);

        // 5. A last element, and this time the caps page is applied with a choice.
        clickFreePad(queue);
        picker = pageOf<PickerPage>(w);
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "fakesink");
        QTest::keyClick(picker->findChild<QLineEdit*>("searchBox"), Qt::Key_Return);
        props = pageOf<PropertyPage>(w);
        press(props, "saveButton");
        caps = pageOf<CapsPage>(w);
        QVERIFY(caps);
        caps->findChild<QComboBox*>("mediaCombo")->setCurrentIndex(1);
        caps->findChild<QWidget*>("field_format")->findChild<QComboBox*>()->setCurrentIndex(2);  // NV12
        press(caps, "saveButton");
        QVERIFY(waitFor([&] { return !c->busy() && c->plan().links[2].caps == "video/x-raw, format=(string)NV12"; }, 8000));
        QVERIFY(!w.flows()->capsPage());
        QVERIFY(c->result().complete());
        QVERIFY(c->canRun());
    }

    void severalSinkPadsAreChosenOnThePropertiesPage() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        w.scene()->rebuild();
        w.view()->fitAll();
        QTest::mouseClick(w.view()->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint(w.view(), w.scene()->pad(src, "src", true)->center()));
        auto* picker = pageOf<PickerPage>(w);
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "dualsink");
        press(picker, "saveButton");
        auto* props = pageOf<PropertyPage>(w);
        QVERIFY(props);
        auto* pads = props->findChild<QComboBox*>("sinkPadCombo");
        QVERIFY(pads);
        QStringList items;
        for (int i = 0; i < pads->count(); ++i) items << pads->itemText(i);
        QCOMPARE(items, QStringList({"main", "aux"}));
        pads->setCurrentText("aux");
        press(props, "saveButton");
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 2 && !c->busy(); }, 8000));
        QCOMPARE(c->plan().links[0].sinkPad, std::string("aux"));
        QCOMPARE(c->view(text(c->plan().elements[1].name)).sinks[1].state, PadView::Linked);
        QCOMPARE(c->view(text(c->plan().elements[1].name)).sinks[0].state, PadView::Empty);
        auto* caps = pageOf<CapsPage>(w);
        QVERIFY(caps);
        press(caps, "cancelButton");
    }

    void pickerOffersExistingElementsAndRequestPads() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        QString mix = addAfter(*c, src, "src", "compositor", "sink_%u");
        QString second = addSource(*c, "videotestsrc");
        w.scene()->rebuild();
        w.view()->fitAll();
        QTest::mouseClick(w.view()->viewport(), Qt::LeftButton, Qt::NoModifier, viewPoint(w.view(), w.scene()->pad(second, "src", true)->center()));
        auto* picker = pageOf<PickerPage>(w);
        QVERIFY(picker);
        auto* list = picker->findChild<QListWidget*>("pickerList");
        QCOMPARE(list->item(0)->text(), QString("Elements in the pipeline"));
        QCOMPARE(list->item(1)->text(), mix + " · sink_%u");
        QCOMPARE(list->item(1)->data(HintRole).toString(), QString("Request a pad"));
        QCOMPARE(list->currentItem(), list->item(1));
        press(picker, "saveButton");  // the first row is selected: "Request a pad"
        // The new Request pad is made, an update runs, and the caps page offers the pad settings.
        auto* caps = pageOf<CapsPage>(w);
        QVERIFY(caps);
        QVERIFY(caps->findChild<QWidget*>("padSettings")->isVisible());
        QCOMPARE(c->plan().links.size(), size_t(2));
        QCOMPARE(c->plan().links[1].sinkPad, std::string("sink_%u"));
        caps->findChild<QWidget*>("padEditor_xpos")->findChild<QSpinBox*>()->setValue(320);
        press(caps, "saveButton");
        QVERIFY(waitFor([&] { return !c->busy() && c->plan().links[1].sinkPadProps.count("xpos") == 1; }, 8000));
        QCOMPARE(c->plan().links[1].sinkPadProps.at("xpos"), std::string("320"));
        QCOMPARE(c->view(mix).sinks.size(), 2);
    }

    void linkMenuHasTheFourActions() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        w.contextMenus()->link(src, "src", QPoint(200, 200));
        auto* menu = visibleChild<QMenu>(&w);
        QVERIFY(menu);
        QCOMPARE(menu->objectName(), QString("linkMenu"));
        QStringList texts;
        for (QAction* a : menu->actions()) texts << a->text();
        QCOMPARE(texts, QStringList({"Edit caps…", "Replace element…", "Cut link", "Insert queue"}));
        menu->close();

        w.contextMenus()->element(*w.front(), src, QPoint(200, 200));
        menu = visibleChild<QMenu>(&w);
        QVERIFY(menu);
        texts.clear();
        for (QAction* a : menu->actions()) texts << a->text();
        QCOMPARE(texts, QStringList({"Edit properties…", "&Copy", "&Duplicate", "&Paste", "De&lete"}));  // the Edit menu's own
        menu->close();
        w.contextMenus()->element(*w.front(), convert, QPoint(200, 200));
        menu = visibleChild<QMenu>(&w);
        texts.clear();
        for (QAction* a : menu->actions()) texts << a->text();
        QCOMPARE(texts, QStringList({"Edit properties…", "&Copy", "&Duplicate", "&Paste", "De&lete"}));
        menu->close();
    }

    // The context menus show the window's own commands: the same actions as the menu bar, with the same rules.
    void contextMenusShareTheWindowsCommands() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        w.scene()->setSelectedElements({});
        w.contextMenus()->element(*w.front(), convert, QPoint(200, 200));
        QMenu* menu = visibleChild<QMenu>(&w);
        QVERIFY(menu);
        QCOMPARE(actionOf(menu, "actionCopy"), (*w.commands())[Command::Copy]);
        QCOMPARE(w.front()->selection(), QStringList({convert}));  // the element it was opened on is the selection
        QVERIFY(actionOf(menu, "actionDelete")->isEnabled());
        menu->close();

        // A tab's menu: the tab comes to the front, and its file commands act on it.
        Document* first = w.front();
        w.newDocument();
        emit w.tabs()->customContextMenuRequested(w.tabs()->tabRect(0).center());
        QCOMPARE(w.front(), first);
        QCOMPARE(takeMenu(&w), QStringList({"&Save", "Save &as…", "Re&vert file", "Version &history…", "&Close tab"}));
    }

    void queueInsertAndRemoveThroughTheMenus() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        w.contextMenus()->link(src, "src", QPoint(200, 200));
        choose(&w, "actionInsertQueue");
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 4 && !c->busy(); }, 8000));
        QString queue = text(c->plan().elements.back().name);
        QCOMPARE(text(c->plan().find(queue.toStdString())->factory), QString("queue"));

        w.contextMenus()->element(*w.front(), queue, QPoint(200, 200));
        QVERIFY(actionOf(visibleChild<QMenu>(&w), "actionRemoveQueue"));
        choose(&w, "actionRemoveQueue");
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 3 && !c->busy(); }, 8000));
    }

    void cutAsksFirstAndCancelChangesNothing() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);

        // Free-canvas rule: cutting a link only disconnects the link, leaving downstream elements on the canvas.
        w.contextMenus()->link(src, "src", QPoint(200, 200));
        choose(&w, "actionCutLink");
        QVERIFY(waitFor([&] { return c->plan().links.size() == 1 && !c->busy(); }, 8000));
        QCOMPARE(c->plan().elements.size(), size_t(3));
        QVERIFY(c->plan().find(convert.toStdString()));
        QVERIFY(c->plan().find(sink.toStdString()));
    }

    void replaceAsksFirstAndKeepsTheLinkPad() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);

        w.contextMenus()->link(src, "src", QPoint(200, 200));
        choose(&w, "actionReplace");
        auto* picker = pageOf<PickerPage>(w);
        QVERIFY(picker);
        QVERIFY(w.drawer()->title().startsWith("Replace after"));
        auto* list = picker->findChild<QListWidget*>("pickerList");
        QCOMPARE(list->item(0)->text(), QString("New element"));  // replacing never offers existing elements
        QTest::keyClicks(picker->findChild<QLineEdit*>("searchBox"), "queue");
        press(picker, "saveButton");
        auto* props = pageOf<PropertyPage>(w);
        QVERIFY(props);
        QCOMPARE(props->primary()->text(), QString("Replace"));
        press(props, "saveButton");
        QVERIFY(waitFor([&] { return !c->plan().find(convert.toStdString()) && !c->busy(); }, 8000));
        // Only convert was replaced; sink stays on the free canvas!
        QCOMPARE(c->plan().elements.size(), size_t(3));
        QVERIFY(c->plan().find(sink.toStdString()));
        QCOMPARE(c->plan().links[0].srcElement, src.toStdString());
        QCOMPARE(text(c->plan().elements.back().factory), QString("queue"));
        QVERIFY(pageOf<CapsPage>(w));  // the update succeeded, so the link's caps are offered
    }

    void deleteSourceAsksFirst() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        w.contextMenus()->element(*w.front(), src, QPoint(200, 200));
        choose(&w, "actionDelete");
        // Free-canvas rule: deleting a source only removes the source element; downstream elements remain on canvas.
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 2 && !c->busy(); }, 8000));
        QVERIFY(!c->plan().find(src.toStdString()));
        QVERIFY(c->plan().find(convert.toStdString()));
        QVERIFY(c->plan().find(sink.toStdString()));
    }

    void deleteKeyOnTheCanvasDeletesTheSelectedSourceAfterAsking() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);

        // Deleting a non-source element removes it with auto-heal!
        w.scene()->clearSelection();
        w.scene()->element(convert)->setSelected(true);
        QTest::keyClick(w.view(), Qt::Key_Delete);
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 2 && !c->busy(); }, 8000));
        QCOMPARE(c->plan().links.size(), size_t(1));

        // Deleting a source element removes only the source, leaving sink on the canvas!
        w.scene()->clearSelection();
        w.scene()->element(src)->setSelected(true);
        QTest::keyClick(w.view(), Qt::Key_Delete);
        QVERIFY(waitFor([&] { return c->plan().elements.size() == 1 && !c->busy(); }, 8000));
        QVERIFY(c->plan().find(sink.toStdString()));
    }

    void editPropertiesByDoubleClick() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        w.scene()->rebuild();
        w.view()->fitAll();
        QPointF body = w.scene()->element(src)->pos() + QPointF(120, 12);
        doubleClick(w.view()->viewport(), viewPoint(w.view(), body));
        auto* page = pageOf<PropertyPage>(w);
        QVERIFY(page);
        QVERIFY(!w.drawer()->isOpen());
        QVERIFY(w.panels()->showing(w.propertiesTab()));
        QCOMPARE(w.flows()->propertiesTitle(), "Edit properties · " + fromStd(c->names().element(toStd(src))));
        QCOMPARE(page->primary()->text(), QString("Save"));
        in<QComboBox>(*page, "editor_pattern")->setCurrentIndex(1);
        QVERIFY(!page->findChild<QCheckBox*>("expose_num-buffers"));  // parameters belong to templates only
        press(page, "saveButton");
        QVERIFY(waitFor([&] { return !c->busy() && c->plan().find(src.toStdString())->props.count("pattern"); }, 8000));
        QCOMPARE(c->plan().find(src.toStdString())->props.at("pattern"), std::string("snow"));
        QVERIFY(c->plan().exposed.empty());
        QVERIFY(!w.flows()->propertyPage());
    }

    void editCapsFromTheMenu() {
        MainWindow w(c.get());
        w.show();
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        w.contextMenus()->link(src, "src", QPoint(200, 200));
        choose(&w, "actionEditCaps");
        auto* caps = pageOf<CapsPage>(w);
        QVERIFY(caps);
        QVERIFY(!w.drawer()->isOpen());
        QVERIFY(w.panels()->showing(w.capsTab()));
        QVERIFY(w.flows()->capsTitle().startsWith("Edit caps"));
        QVERIFY(!caps->findChild<QPushButton*>("skipButton"));  // Skip belongs to the step after an add
        caps->findChild<QComboBox*>("mediaCombo")->setCurrentIndex(1);
        auto* width = caps->findChild<QWidget*>("field_width");
        width->findChild<QCheckBox*>()->click();
        width->findChild<QSpinBox*>()->setValue(1280);
        press(caps, "saveButton");
        QVERIFY(waitFor([&] { return !c->busy() && !c->plan().links[0].caps.empty(); }, 8000));
        QCOMPARE(c->plan().links[0].caps, std::string("video/x-raw, width=(int)1280"));
        QVERIFY(c->result().complete());
        // Open it again: the page starts from the choice. Cancel changes nothing.
        w.flows()->editCaps(src, "src");
        caps = pageOf<CapsPage>(w);
        QVERIFY(caps);
        QCOMPARE(caps->caps(), QString("video/x-raw, width=(int)1280"));
        press(caps, "cancelButton");
        QCOMPARE(c->plan().links[0].caps, std::string("video/x-raw, width=(int)1280"));
    }

    void editingIsRefusedDuringPlayback() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        need(*c, "fakesink");
        QVERIFY(c->addAfter(src, "src", "fakesink", "sink", {}));
        QVERIFY(settle(*c));
        QVERIFY(c->run());
        QVERIFY(c->editingBlocked());
        w.flows()->fromFreePad(src, "src");
        w.scene()->linkContext(src, "src", QPoint(10, 10));  // the locked canvas offers no menu
        w.scene()->elementContext(src, QPoint(10, 10));
        QVERIFY(!c->deleteElement(src, false));
        w.flows()->editProperties(src);
        w.flows()->chooseElement();
        QTest::qWait(50);
        QVERIFY(!visibleChild<QMenu>(&w));
        QVERIFY(!visibleChild<QDialog>(&w));
        QVERIFY(!w.drawer()->isOpen());
        QVERIFY(waitFor([&] { return !c->running(); }));
        QCOMPARE(c->plan().elements.size(), size_t(2));
    }

    // A file opens once: a second open while the first still loads finds its tab, and Save as onto it is refused.
    void aFileOpensOnceEvenWhileItLoads() {
        QTemporaryDir root;
        const QString file = root.filePath("once.conf");
        gps::Graph g;
        const std::string src = g.addSource("videotestsrc");
        g.addAfter(src, "src", "avidemux", "sink");  // a factory no other test asked about: the open waits for it
        std::string error;
        QVERIFY(g.save(file.toStdString(), &error));
        MainWindow w(c.get());
        w.show();
        w.openFile(file);
        QVERIFY(w.front()->controller()->busy());
        w.openFile(file);
        QCOMPARE(w.documents().size(), 1);
        QVERIFY(waitFor([&] { return !w.front()->controller()->busy(); }));
        QCOMPARE(w.front()->path(), file);

        Document* other = w.newDocument();
        QVERIFY(!addSource(*other->controller(), "videotestsrc").isEmpty());
        QString asked;
        answer(&w, "ok", &asked);
        QVERIFY(!w.saveFlow()->saveAs(other, file));
        QVERIFY(asked.contains("once.conf is open in another tab"));
        QVERIFY(other->controller()->target().needsDestination());
    }

    // Closing a running tab with unsaved work: Save stops the run first, then writes, and the tab goes.
    void closingARunningTabSavesItOnceTheRunStops() {
        QTemporaryDir root;
        const QString file = root.filePath("run.conf");
        MainWindow w(c.get());
        w.show();
        Document* d = w.front();
        const QString src = addSource(*c, "videotestsrc");
        QVERIFY(w.saveFlow()->saveAs(d, file));
        QVERIFY(!addAfter(*c, src, "src", "fakesink").isEmpty());
        QVERIFY(c->run());
        answer(&w, "saveChangesSave");
        QVERIFY(w.closeDocument(d));
        QVERIFY(!c->running());
        gps::Graph saved;
        std::string error;
        QVERIFY(gps::Graph::load(file.toStdString(), &saved, &error));
        QCOMPARE(saved.elements.size(), size_t(2));
    }

    void runFromTheToolbarLogsTheHelper() {
        MainWindow w(c.get());
        w.show();
        QString src = addSource(*c, "videotestsrc");
        addAfter(*c, src, "src", "fakesink");
        auto* run = w.findChild<QAction*>("actionRun");
        QVERIFY(run->isEnabled());
        QCOMPARE(run->text(), QString("&Run"));
        QSignalSpy ended(c.get(), &Controller::runFinished);
        run->trigger();
        refreshed();
        QCOMPARE(run->text(), QString("&Stop"));
        QVERIFY(waitFor([&] { return ended.count() == 1; }));
        refreshed();
        QCOMPARE(run->text(), QString("&Run"));
        QVERIFY(w.logPanel()->toPlainText().contains("Run finished: eos"));
    }

    void openFileFitsAndShowsThePlan() {
        QString src, convert, sink;
        QTemporaryDir dir;
        QString path = dir.filePath("keep.conf");
        {
            src = addSource(*c, "videotestsrc");
            convert = addAfter(*c, src, "src", "videoconvert");
            sink = addAfter(*c, convert, "src", "fakesink");
            QVERIFY(saved(saveAs(*c, path)));
        }
        auto other = makeController();
        MainWindow w(other.get());
        w.show();
        w.openFile(path);
        QVERIFY(waitFor([&] { return other->result().valid() && !other->busy() && w.scene()->element(sink) != nullptr; }, 8000));
        QCOMPARE(w.scene()->links().size(), 2);
        refreshed();
        QCOMPARE(w.windowTitle(), BuildInfo::title() + " " + BuildInfo::version() + " – keep.conf");
        QVERIFY(w.findChild<QAction*>("actionRun")->isEnabled());
        QTest::qWait(100);
        // fitAll ran: the whole pipeline is inside the viewport
        QRectF items = w.scene()->itemsBoundingRect();
        QVERIFY(w.view()->viewport()->rect().contains(w.view()->mapFromScene(items).boundingRect().adjusted(5, 5, -5, -5)));
    }

    void rightDrawerFiltersBottomCapsTabAndDynamicPipeCaps() {
        MainWindow w(c.get());
        w.show();

        // 1. Right drawer position: drawer sits to the right of the canvas view.
        w.findChild<QAction*>("actionAddElement")->trigger();
        auto* picker = pageOf<PickerPage>(w);
        QVERIFY(picker);
        QVERIFY(waitFor([&] { return w.drawer()->width() == Drawer::kWidth; }, 2000));
        QVERIFY(w.drawer()->geometry().left() >= w.view()->geometry().right() - 2);

        // 2. Role and Pad Presence filters in PickerPage.
        picker->setRoleFilter(PickerPage::RoleSrc);
        auto* list = picker->findChild<QListWidget*>("pickerList");
        for (int i = 1; i < list->count(); ++i) {
            if (!list->item(i)->isHidden()) QVERIFY(c->services().catalog().traits()(list->item(i)->text().toStdString()).isSource);
        }
        picker->setRoleFilter(PickerPage::RoleAll);
        picker->setPresenceFilter(PickerPage::PresenceRequest);
        bool sawTee = false, sawCompositor = false;
        for (int i = 1; i < list->count(); ++i) {
            if (!list->item(i)->isHidden()) {
                sawTee |= (list->item(i)->text() == "tee");
                sawCompositor |= (list->item(i)->text() == "compositor");
            }
        }
        QVERIFY(sawTee && sawCompositor);
        w.drawer()->dismiss();

        // 3. Dynamic Pipe Caps formatting and layout rules.
        QString rawCaps = "video/x-raw, format=(string)NV12, width=(int)1920, height=(int)1080";
        const QString compactCaps = capslabel::singleLine(rawCaps);
        QVERIFY(compactCaps.size() <= 60);
        QVERIFY(compactCaps.contains("width=1920"));
        QVERIFY(compactCaps.endsWith("…"));
        QCOMPARE(capslabel::multiLine(rawCaps), QString("mediatype=video/x-raw\nformat=NV12\nwidth=1920\nheight=1080"));
        QCOMPARE(LinkItem::decideLayout(250.0, 200.0, 1.0), LinkItem::SingleLine);
        QCOMPARE(LinkItem::decideLayout(120.0, 200.0, 1.0), LinkItem::MultiLine);
        QCOMPARE(LinkItem::decideLayout(650.0, 200.0, 1.0), LinkItem::AtPads);
        QCOMPARE(LinkItem::decideLayout(150.0, 200.0, 1.8), LinkItem::AtPads);

        // 4. Bottom Caps tab opens when a link is clicked.
        QString src, convert, sink;
        buildChain(w, &src, &convert, &sink);
        w.openCapsTab(src, "src");
        QVERIFY(w.panels()->showing(w.capsTab()));
        QVERIFY(w.capsTab()->findChild<CapsPage*>());

        // 5. Rubber-band marquee selection mode on CanvasView & collapsed Template rendering on CanvasScene.
        QCOMPARE(w.view()->dragMode(), QGraphicsView::RubberBandDrag);
        QTemporaryDir customDir;
        useUserDir(customDir.path());
        QVERIFY(saveTemplate(*c, "ConvSinkPreset", {convert, sink}));
        scene_rebuild(w);
        // Both convert and sink map to the same collapsed ElementItem on the scene, and only 1 external link is drawn!
        QVERIFY(w.scene()->element(convert) != nullptr);
        QCOMPARE(w.scene()->element(convert), w.scene()->element(sink));
        QVERIFY(w.scene()->element(convert)->view().isTemplate);
        QCOMPARE(w.scene()->links().size(), 1);

        // Expanding the template shows individual ElementItems and all 2 links.
        QString grp = c->outline().collapsedTemplate(convert);
        c->setTemplateExpanded(grp, true);
        scene_rebuild(w);
        QVERIFY(w.scene()->element(convert) != w.scene()->element(sink));
        QCOMPARE(w.scene()->links().size(), 2);
    }

private:
    // The scene rebuilds when the event loop turns; tests that look at it right away rebuild by hand.
    void scene_rebuild(MainWindow& w) {
        w.scene()->rebuild();
        w.view()->fitAll();
    }
};

QTEST_MAIN(WidgetsTest)
#include "widgets_test.moc"
