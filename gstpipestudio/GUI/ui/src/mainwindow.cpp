// mainwindow.cpp
#include "mainwindow.h"
#include "windowchrome.h"
#include "aboutdialog.h"
#include "buildinfo.h"
#include "pipelinefiledialog.h"

#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QMouseEvent>
#include <QPointer>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QPushButton>
#include <QLocale>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QDialog>
#include <algorithm>
#include <tuple>
#include <QActionGroup>

#include "canvasscene.h"
#include "canvasview.h"
#include "capspage.h"
#include "appservices.h"
#include "controller.h"
#include "drawer.h"
#include "document.h"
#include "documentbackup.h"
#include "documentregistry.h"
#include "elementitem.h"
#include "explorer.h"
#include "flows.h"
#include "linkitem.h"
#include "logpanel.h"
#include "propertypage.h"
#include "setuppage.h"
#include "settings.h"
#include "templates.h"
#include "newprojectdialog.h"
#include "saveflow.h"
#include "savetarget.h"
#include "panels.h"
#include "tabclosebutton.h"
#include "theme.h"
#include "versionhistory.h"
#include "updatepanel.h"
#include "telemetrypanel.h"
#include "validationindicator.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QInputDialog>
#include <QVBoxLayout>
#include <set>

MainWindow::MainWindow(Controller* controller, QWidget* parent)
    : QMainWindow(parent), services_(controller->services()), saveFlow_(new SaveFlow(this)), registry_(new DocumentRegistry(this)) {
    setObjectName("mainWindow");
    setWindowFlag(Qt::FramelessWindowHint);
    setWindowIcon(QIcon(":/brand/gstpipestudio.png"));
    setMinimumSize(960, 600);
    resize(1360, 860);

    // The tabs over the canvas, as editors have them: one per open file.
    auto* centre = new QWidget(this);
    auto* column = new QVBoxLayout(centre);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    tabs_ = new QTabBar(centre);
    tabs_->setObjectName("documentTabs");
    tabs_->setDocumentMode(true);
    tabs_->setMovable(true);
    tabs_->setExpanding(false);
    tabs_->setElideMode(Qt::ElideMiddle);
    tabs_->setUsesScrollButtons(true);
    tabs_->installEventFilter(this);
    pages_ = new QStackedWidget(centre);
    pages_->setObjectName("documentPages");
    column->addWidget(tabs_);
    column->addWidget(pages_, 1);
    setCentralWidget(centre);
    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (Document* d = docAt(index); d && d != doc_) activate(d);
    });
    connect(registry_, &DocumentRegistry::changedOnDisk, this, &MainWindow::onChangedOnDisk);
    connect(registry_, &DocumentRegistry::deletedOnDisk, this, [this](Document* d) {
        statusBar()->showMessage(d->controller()->target().title() + " was deleted on disk: save to keep it", 10000);
    });

    buildPanels();
    addDocument(controller);
    explorer_ = new Explorer(this);
    panels_->add(explorer_, "Explorer", 0, Qt::LeftDockWidgetArea);
    connect(explorer_, &Explorer::openRequested, this, &MainWindow::openFile);
    connect(explorer_, &Explorer::templateRequested, this, &MainWindow::openTemplate);
    connect(explorer_, &Explorer::templateMenuRequested, this, [this](const QString& name, const QPoint& at) { flows_->templateMenu(name, at); });
    resizeDocks({panels_->dock(log_)}, {240}, Qt::Vertical);
    resizeDocks({panels_->dock(explorer_)}, {260}, Qt::Horizontal);
    panels_->show(updates_);

    buildActions();
    auto* menus = menuBar();
    auto* chrome = new WindowChrome(this, menus);
    setMenuWidget(chrome);

    restoreGeometry(Settings::windowGeometry());
    restoreState(Settings::windowState());
    built_ = true;
    // GStreamer is checked once for the whole window; every tab shows the answer.
    connect(&services_.environment(), &EnvironmentService::checked, this, &MainWindow::onEnvironment);
    services_.environment().check();
    activate(doc_);
}

bool MainWindow::showingSetup() const { return centre_->currentWidget() == setup_; }

void MainWindow::buildActions() {
    auto make = [this](const QString& name, const QString& text, const QList<QKeySequence>& keys, auto slot) {
        auto* a = new QAction(text, this);
        a->setObjectName(name);
        a->setShortcuts(keys);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    auto* newPlan = make("actionNew", "&New", {QKeySequence::New}, [this] {
        if (!doc_->pristine()) newDocument();
    });
    auto* open = make("actionOpen", "&Open…", {QKeySequence::Open}, [this] {
        QString path = PipelineFileDialog::choose(this, false, Settings::lastDir());
        if (!path.isEmpty()) openFile(path);
    });
    auto* history = make("actionVersionHistory", "Version &history…", {}, [this] { showVersionHistory(); });
    auto* import = make("actionImportLaunch", "Import gst-launch pipeline…", {}, [this] {
        bool accepted = false;
        QString description = QInputDialog::getMultiLineText(this, "Import gst-launch pipeline",
            "Paste a gst-launch-1.0 command or its pipeline description.\n"
            "The pipeline runs briefly in the background to discover dynamic pads.", QString(), &accepted);
        if (!accepted || description.trimmed().isEmpty()) return;
        Document* d = blankTab();
        d->opening("imported");
        d->controller()->importLaunch(description);
    });
    auto* newProject = make("actionNewProject", "New &project…", {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N)}, [this] {
        const QString conf = NewProjectDialog::ask(this);
        if (!conf.isEmpty()) blankTab()->controller()->newPlan(std::make_unique<ProjectConfTarget>(conf));
    });
    // A canvas never saved becomes a project: its name is asked, and its first save makes the folder.
    auto* save = make("actionSave", "&Save", {QKeySequence::Save}, [this] { saveFlow_->save(doc_); });
    auto* saveAll = make("actionSaveAll", "Save a&ll", {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S)}, [this] { saveFlow_->saveAll(documents()); });
    auto* revert = make("actionRevert", "Re&vert file", {}, [this] { controller_->revert(); });
    auto* closeTab = make("actionCloseTab", "&Close tab", {QKeySequence(Qt::CTRL | Qt::Key_W), QKeySequence(Qt::CTRL | Qt::Key_F4)},
                          [this] { closeDocument(doc_); });
    auto step = [this](int by) { tabs_->setCurrentIndex((tabs_->currentIndex() + by + tabs_->count()) % tabs_->count()); };
    auto* nextTab = make("actionNextTab", "&Next tab", {QKeySequence(Qt::CTRL | Qt::Key_Tab), QKeySequence(Qt::CTRL | Qt::Key_PageDown)},
                         [step] { step(1); });
    auto* previousTab = make("actionPreviousTab", "Pre&vious tab",
                             {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab), QKeySequence(Qt::CTRL | Qt::Key_PageUp)}, [step] { step(-1); });
    auto* saveAs = make("actionSaveAs", "Save &as…", {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)}, [this] { saveFlow_->saveAs(doc_); });
    auto* update = make("actionUpdate", "&Validate", {QKeySequence(Qt::CTRL | Qt::Key_R), QKeySequence(Qt::Key_F5)}, [this] { controller_->refresh(); });
    auto* run = make("actionRun", "&Run", {QKeySequence(Qt::CTRL | Qt::Key_Return)}, [this] { toggleRun(); });
    auto* addElement = make("actionAddElement", "Add &element…", {QKeySequence(Qt::CTRL | Qt::Key_E), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)},
                            [this] { flows_->chooseElement(); });
    addElement->setIconText("Add element");
    auto* addTemplate = make("actionAddTemplate", "Add &template…", {QKeySequence(Qt::CTRL | Qt::Key_T)}, [this] { flows_->chooseTemplate(); });
    addTemplate->setIconText("Add template");
    auto* undoAct = make("actionUndo", "&Undo", {QKeySequence::Undo}, [this] { controller_->undo(); });
    undoAct->setIconText("Undo");
    auto* redoAct = make("actionRedo", "&Redo", {QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)}, [this] { controller_->redo(); });
    redoAct->setIconText("Redo");
    auto* dupAct = make("actionDuplicate", "&Duplicate", {QKeySequence(Qt::CTRL | Qt::Key_D)}, [this] { duplicateSelected(); });
    auto* selAllAct = make("actionSelectAll", "Select &All", {QKeySequence::SelectAll}, [this] { selectAll(); });
    auto* delAct = make("actionDelete", "De&lete", {QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)}, [this] { deleteSelected(); });
    delAct->setIconText("Delete");
    auto* copyAct = make("actionCopy", "&Copy", {QKeySequence::Copy}, [this] { copySelected(); });
    auto* cutAct = make("actionCut", "Cu&t", {QKeySequence::Cut}, [this] { cutSelected(); });
    auto* pasteAct = make("actionPaste", "&Paste", {QKeySequence::Paste}, [this] { pasteClipboard(); });
    auto* connectSelAct = make("actionConnectSelected", "Connect &selected", {QKeySequence(Qt::CTRL | Qt::Key_L)}, [this] { connectSelected(); });
    auto* saveCustomAct = make("actionSaveTemplateElement", "Save as &template…", {QKeySequence(Qt::CTRL | Qt::Key_G)}, [this] { saveSelectedAsTemplate(); });
    auto* templateParams = make("actionTemplateParameters", "Template &parameters…", {}, [this] { flows_->editTemplateParameters(); });
    saveCustomAct->setIconText("Save as template");
    auto* find = make("actionFind", "&Find element", {QKeySequence::Find}, [this] {
        if (drawer_->isOpen()) drawer_->focusSearch();
        else if (flows_->propertyPage()) flows_->propertyPage()->focusFirst();
    });
    auto* fit = make("actionFit", "Zoom to &fit", {QKeySequence(Qt::CTRL | Qt::Key_0)}, [this] { view_->fitAll(); });
    auto* arrange = make("actionArrange", "Arrange &pipeline", {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L)}, [this] { controller_->niceAlign(); });
    auto* zoomIn = make("actionZoomIn", "Zoom &in", {QKeySequence::ZoomIn}, [this] { view_->zoomBy(1.25); });
    auto* zoomOut = make("actionZoomOut", "Zoom &out", {QKeySequence::ZoomOut}, [this] { view_->zoomBy(0.8); });
    auto* quit = make("actionQuit", "&Quit", {QKeySequence::Quit}, [this] { close(); });
    auto* support = make("actionSupport", "&Request support…", {}, [this] { controller_->requestSupport("Problem report"); });
    auto* about = make("actionAbout", "&About GstPipeStudio", {}, [this] { showAbout(); });
    auto* gstInfo = make("actionGstInfo", "&GStreamer information", {}, [this] {
        const StartupResult& env = services_.environment().result();
        QMessageBox::information(this, "GStreamer information", env.version.isEmpty() ? "GStreamer was not checked or not found." : env.statusText());
    });

    // The menus as desktop applications have them: File, Edit, View, then the application's own, then Help.
    QMenu* file = menuBar()->addMenu("&File");
    file->addActions({newProject, newPlan, open});
    QMenu* recent = file->addMenu("Open &recent");
    recent->setObjectName("menuRecent");
    connect(recent, &QMenu::aboutToShow, this, [this, recent] {
        recent->clear();
        const QStringList files = Settings::recentFiles();
        for (const QString& path : files) recent->addAction(QFileInfo(path).fileName(), this, [this, path] { openFile(path); })->setToolTip(path);
        if (files.isEmpty()) recent->addAction("No recent files")->setEnabled(false);
    });
    file->addSeparator();
    file->addActions({save, saveAs, saveAll});
    file->addSeparator();
    file->addActions({revert, closeTab});
    file->addSeparator();
    file->addActions({import, history});
    file->addSeparator();
    file->addAction(quit);

    QMenu* edit = menuBar()->addMenu("&Edit");
    edit->addActions({undoAct, redoAct});
    edit->addSeparator();
    edit->addActions({cutAct, copyAct, pasteAct, dupAct, delAct});
    edit->addSeparator();
    edit->addActions({selAllAct, find});

    QMenu* viewMenu = menuBar()->addMenu("&View");
    QMenu* panels = viewMenu->addMenu("&Panels");
    panels->addActions(panels_->toggles());
    panels->addSeparator();
    panels->addAction("&Reset layout", this, [this] { panels_->reset(updates_); })->setObjectName("actionResetLayout");
    viewMenu->addActions({nextTab, previousTab});
    viewMenu->addSeparator();
    viewMenu->addActions({zoomIn, zoomOut, fit});
    viewMenu->addSeparator();
    viewMenu->addAction(arrange);
    // The view preferences are the process's: the menu sets them once, every tab follows.
    ViewPreferences& preferences = services_.view();
    auto* automatic = viewMenu->addAction("&Auto alignment");
    automatic->setObjectName("actionAutoAlignment");
    automatic->setCheckable(true);
    automatic->setToolTip("Align the whole canvas after every edit; a dragged box slides back into place");
    connect(automatic, &QAction::triggered, this, [this](bool on) {
        services_.view().setAutoAlign(on);
        if (on) controller_->niceAlign();
    });
    // Link style: the next nice alignment routes links as curves or right-angle lines.
    QMenu* linkStyle = viewMenu->addMenu("&Link style");
    auto* styles = new QActionGroup(this);
    for (auto [name, routing, label] : {std::tuple{"actionLinkCurves", layout::Routing::Curves, "&Curves"},
                                        std::tuple{"actionLinkRightAngles", layout::Routing::RightAngles, "&Right angles"}}) {
        QAction* style = linkStyle->addAction(label);
        style->setObjectName(name);
        style->setCheckable(true);
        styles->addAction(style);
        connect(style, &QAction::triggered, this, [this, routing = routing] {
            services_.view().setRouting(routing);
            controller_->niceAlign();
        });
    }
    auto* connectedMove = viewMenu->addAction("Move connected elements together");
    connectedMove->setObjectName("actionConnectedMove"); connectedMove->setCheckable(true);
    connectedMove->setToolTip("Drag an element to move its connected component; undo restores all positions");
    connect(connectedMove, &QAction::triggered, &preferences, &ViewPreferences::setConnectedMove);
    showViewPreferences();
    connect(&preferences, &ViewPreferences::changed, this, &MainWindow::showViewPreferences);

    QMenu* pipeline = menuBar()->addMenu("&Pipeline");
    pipeline->addActions({addElement, addTemplate, saveCustomAct, templateParams, connectSelAct});
    pipeline->addSeparator();
    pipeline->addActions({update, run});

    QMenu* help = menuBar()->addMenu("&Help");
    help->addActions({gstInfo, support});
    help->addSeparator();
    help->addAction(about);

    QToolBar* bar = addToolBar("Tools");
    bar->setObjectName("toolbar");
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bar->addActions({newPlan, open, save, saveAs});
    bar->addSeparator();
    bar->addActions({undoAct, redoAct});
    bar->addSeparator();
    bar->addActions({update, run});
    bar->addSeparator();
    bar->addAction(addElement);
    bar->addAction(addTemplate);
    bar->addAction(saveCustomAct);
    bar->addSeparator();
    bar->addAction(delAct);
    bar->widgetForAction(run)->setObjectName("primaryAction");
}

void MainWindow::buildPanels() {
    panels_ = new Panels(this);
    state_ = new QLabel(this);
    state_->setObjectName("stateLabel");
    environment_ = new QLabel(this);
    environment_->setObjectName("environmentLabel");
    environment_->setStyleSheet("color: " + Theme::muted().name() + ";");
    command_ = new QLabel(this);
    command_->setObjectName("commandLabel");
    command_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    command_->setStyleSheet("font-family: 'DejaVu Sans Mono'; color: " + Theme::muted().name() + ";");
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(state_);
    statusBar()->addWidget(environment_);
    statusBar()->addPermanentWidget(command_);
}

// ---- documents

// The View menu's checks: what the preferences are now.
void MainWindow::showViewPreferences() {
    const ViewPreferences& preferences = services_.view();
    findChild<QAction*>("actionAutoAlignment")->setChecked(preferences.autoAlign());
    findChild<QAction*>("actionLinkCurves")->setChecked(preferences.routing() == layout::Routing::Curves);
    findChild<QAction*>("actionLinkRightAngles")->setChecked(preferences.routing() == layout::Routing::RightAngles);
    findChild<QAction*>("actionConnectedMove")->setChecked(preferences.connectedMove());
}

Document* MainWindow::addDocument(Controller* controller) {
    auto* d = new Document(services_, controller, this);
    if (d->controller()->target().needsDestination()) {  // "Untitled-N": the lowest N no other tab has
        QList<const SaveTarget*> open;
        for (const Document* other : std::as_const(docs_)) open << &other->controller()->target();
        d->controller()->setTarget(std::make_unique<UntitledTarget>(UntitledTarget::lowestFree(open)));
    }
    for (auto [page, title, room] : {std::tuple{static_cast<QWidget*>(d->log()), "Log", 0}, {d->updates(), "Validation", 0},
                                     {d->telemetry(), "Live metrics", 400}, {d->propsTab(), "Properties", 400}, {d->capsTab(), "Caps", 400}})
        panels_->add(page, title, room);
    d->flows()->setBottomPanels(panels_, d->propsTab(), d->capsTab(), d->updates());
    statusBar()->addPermanentWidget(d->indicator());
    pages_->addWidget(d->centre());
    docs_ << d;
    const int index = tabs_->addTab(d->title());
    tabs_->setTabData(index, QVariant::fromValue(static_cast<void*>(d)));
    auto* close = new TabCloseButton(tabs_);
    connect(close, &QToolButton::clicked, this, [this, d] { closeDocument(d); });
    tabs_->setTabButton(index, QTabBar::RightSide, close);
    registry_->track(d);
    wire(d);
    activate(d);
    return d;
}

// What of a document the window answers: menus on its canvas, questions about templates, its tab and the state shown.
void MainWindow::wire(Document* d) {
    Controller* c = d->controller();
    CanvasScene* scene = d->scene();
    auto front = [this, d] { return d == doc_; };
    connect(scene, &CanvasScene::elementMenuRequested, this, &MainWindow::onElementMenuRequested, Qt::QueuedConnection);
    connect(scene, &CanvasScene::templateMenuRequested, this, &MainWindow::showTemplateMenu, Qt::QueuedConnection);
    connect(scene, &CanvasScene::canvasContextMenuRequested, this, [this](const QPoint& screenPos, const QPointF&) { showCanvasMenu(screenPos); }, Qt::QueuedConnection);
    connect(scene, &QGraphicsScene::selectionChanged, this, [this, front] { if (front()) refreshState(); });
    connect(d->view(), &CanvasView::deletePressed, this, &MainWindow::deleteSelected);
    connect(d->flows(), &Flows::editTemplateRequested, this, &MainWindow::openTemplate);
    connect(c, &Controller::templateChanged, this, [this, front](const QString& id) { if (front()) askTemplateChange(id); }, Qt::QueuedConnection);
    connect(c, &Controller::templatesOutdated, this, [this, d, front](const QStringList& ids) {
        if (front()) askTemplatesOutdated(ids);
        else d->deferOutdated(ids);
    }, Qt::QueuedConnection);
    connect(c, &Controller::changed, this, [this, d, front] {
        refreshTab(d);
        if (front()) refreshState();
    });
    connect(c, &Controller::notice, this, [this, front](const QString& text) { if (front()) statusBar()->showMessage(text, 10000); });
}

void MainWindow::activate(Document* d) {
    if (!d) return;
    doc_ = d;
    controller_ = d->controller();
    scene_ = d->scene();
    view_ = d->view();
    flows_ = d->flows();
    drawer_ = d->drawer();
    centre_ = d->centre();
    setup_ = d->setup();
    log_ = d->log();
    updates_ = d->updates();
    telemetry_ = d->telemetry();
    propsTab_ = d->propsTab();
    capsTab_ = d->capsTab();
    indicator_ = d->indicator();
    pages_->setCurrentWidget(d->centre());
    for (QWidget* page : d->panelPages()) panels_->bring(page);
    for (Document* other : std::as_const(docs_)) other->indicator()->setVisible(other == d);
    {
        const QSignalBlocker quiet(tabs_);
        tabs_->setCurrentIndex(indexOf(d));
    }
    refreshTab(d);
    if (!built_) return;
    onEnvironment();
    const QStringList outdated = d->takeOutdated();
    if (!outdated.isEmpty()) QTimer::singleShot(0, this, [this, d, outdated] { if (d == doc_) askTemplatesOutdated(outdated); });
    if (d->takeChangedOnDisk()) QTimer::singleShot(0, this, [this, d] { if (d == doc_) onChangedOnDisk(d); });
}

void MainWindow::refreshTab(Document* d) {
    const int index = indexOf(d);
    if (index < 0) return;
    tabs_->setTabText(index, d->title());
    tabs_->setTabToolTip(index, d->path().isEmpty() ? d->title() : d->path());
    if (auto* close = qobject_cast<TabCloseButton*>(tabs_->tabButton(index, QTabBar::RightSide)))
        close->setDirty(d->controller()->hasUnsavedWork());
    showSaveAll();
}

// Save all is on while any tab has unsaved work: a change in any tab, or a closed one, can turn it on or off.
void MainWindow::showSaveAll() {
    if (auto* saveAll = findChild<QAction*>("actionSaveAll"))
        saveAll->setEnabled(std::any_of(docs_.cbegin(), docs_.cend(), [](const Document* d) { return d->controller()->hasUnsavedWork(); }));
}

Document* MainWindow::docAt(int index) const {
    return index < 0 || index >= tabs_->count() ? nullptr : static_cast<Document*>(tabs_->tabData(index).value<void*>());
}

int MainWindow::indexOf(Document* d) const {
    for (int i = 0; i < tabs_->count(); ++i)
        if (docAt(i) == d) return i;
    return -1;
}

QList<Document*> MainWindow::documents() const {
    QList<Document*> out;
    for (int i = 0; i < tabs_->count(); ++i) out << docAt(i);
    return out;
}

Document* MainWindow::newDocument() { return addDocument(nullptr); }

Document* MainWindow::blankTab() { return doc_ && doc_->pristine() ? doc_ : newDocument(); }

bool MainWindow::closeDocument(Document* d) {
    if (!d || !docs_.contains(d) || !saveFlow_->confirmClose({d})) return false;
    d->backup()->discard();
    if (d->controller()->running()) d->controller()->stopRun();
    docs_.removeAll(d);  // first: its Untitled-N is free for the tab that may replace it
    if (docs_.isEmpty()) newDocument();  // the window always has a canvas
    if (d == doc_) activate(docAt(indexOf(d) + 1) ? docAt(indexOf(d) + 1) : docAt(indexOf(d) - 1));
    tabs_->removeTab(indexOf(d));
    pages_->removeWidget(d->centre());
    delete d;
    refreshState();
    return true;
}

// The tabs that are open, by their targets' session keys, with their backups, and the one in front. An untitled tab
// without unsaved work is not kept.
void MainWindow::saveSession() {
    Settings::Session session;
    for (const Document* d : documents()) {
        const bool backedUp = d->backup()->active();
        if (d->controller()->target().needsDestination() && !backedUp) continue;
        if (d == doc_) session.front = session.tabs.size();
        session.tabs << d->controller()->target().sessionKey();
        session.backups << (backedUp ? d->backup()->id() : QString());
    }
    Settings::setSession(session);
}

void MainWindow::restoreSession() {
    const Settings::Session session = Settings::session();
    QList<Document*> opened;
    for (const Backup& tab : services_.backups().reopen(session)) opened << reopen(tab);
    if (session.front >= 0 && session.front < opened.size() && opened[session.front]) activate(opened[session.front]);
}

// Without a backup there must be a file to read: an untitled tab without changes, or a file gone, is not reopened.
Document* MainWindow::reopen(const Backup& tab) {
    std::unique_ptr<SaveTarget> target = SaveTarget::fromKey(tab.key, services_.library());
    if (!target || (tab.id.isEmpty() && !QFileInfo::exists(target->path()))) return nullptr;
    return open(std::move(target), tab.id);
}

// Someone else changed a tab's file: without unsaved work it is read again; with some, the user chooses, when the tab
// is in front. Asked again when a deferred tab comes to the front, it may be clean or saved by then.
void MainWindow::onChangedOnDisk(Document* d) {
    refreshTab(d);                                // a deleted file that is back loses its mark
    if (!registry_->changedSinceRead(d)) return;  // saved or reloaded meanwhile, or back as it was
    if (!d->controller()->hasUnsavedWork()) {
        d->controller()->reload();
        statusBar()->showMessage(d->title() + " changed on disk and was reloaded", 10000);
    } else if (d == doc_) {
        askReload(d);
    } else {
        d->deferChangedOnDisk();
    }
}

// Opened, not run: the event loop goes on (saves, runs, the watcher) while it waits.
void MainWindow::askReload(Document* d) {
    auto* box = new QMessageBox(QMessageBox::Warning, "File changed on disk",
                                d->title() + " was changed on disk by another program.", QMessageBox::NoButton, this);
    box->setObjectName("changedOnDisk");
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setInformativeText("Reload it and lose your changes, or keep your version?");
    QPushButton* reload = box->addButton("Reload", QMessageBox::DestructiveRole);
    QPushButton* keep = box->addButton("Keep mine", QMessageBox::RejectRole);
    reload->setObjectName("reloadFromDisk");
    keep->setObjectName("keepMine");
    box->setDefaultButton(keep);
    QPointer<Document> asked(d);
    connect(box, &QMessageBox::finished, this, [this, box, reload, asked] {
        if (!asked) return;
        if (box->clickedButton() == reload) asked->controller()->reload();
        else registry_->accept(asked);
    });
    box->open();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == tabs_ && event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::MiddleButton) {
            if (Document* d = docAt(tabs_->tabAt(mouse->position().toPoint()))) closeDocument(d);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// A disabled action says why, in its tooltip.
void MainWindow::setReason(QAction* action, const QString& text, const QString& reason) {
    action->setToolTip(reason.isEmpty() ? text : text + " is off. " + reason);
}

void MainWindow::refreshState() {
    if (!built_) return;
    auto act = [this](const char* name) { return findChild<QAction*>(name); };
    QStringList templates;
    for (const TemplateInfo& t : services_.library().list()) templates << t.name;
    explorer_->present(controller_->target().projectConf(), templates);
    bool blocked = services_.environment().blocked();
    bool busy = controller_->busy();
    bool editBlocked = controller_->editingBlocked();
    bool hasPlan = !controller_->plan().empty();

    act("actionNew")->setEnabled(!busy && !blocked);
    act("actionOpen")->setEnabled(!busy && !blocked);
    act("actionImportLaunch")->setEnabled(!busy && !blocked);
    act("actionAddElement")->setEnabled(!editBlocked && !blocked);
    act("actionAddTemplate")->setEnabled(!editBlocked && !blocked);
    act("actionSaveTemplateElement")->setEnabled(!editBlocked && !scene_->selectedElementNames().isEmpty());
    act("actionTemplateParameters")->setEnabled(!editBlocked && !controller_->target().libraryTemplate().isEmpty());
    act("actionDelete")->setEnabled(!editBlocked && !scene_->selectedItems().isEmpty());
    act("actionUndo")->setEnabled(controller_->canUndo());
    act("actionRedo")->setEnabled(controller_->canRedo());
    act("actionSave")->setEnabled(controller_->canSave());
    act("actionSaveAs")->setEnabled(controller_->canSave());
    showSaveAll();
    act("actionRevert")->setEnabled(controller_->canRevert());
    act("actionVersionHistory")->setEnabled(!busy && !doc_->path().isEmpty() && !VersionHistory::versions(doc_->path()).isEmpty());
    act("actionUpdate")->setEnabled(!busy && hasPlan);
    act("actionArrange")->setEnabled(!editBlocked && hasPlan);
    act("actionRun")->setText(controller_->running() ? "&Stop" : "&Run");
    act("actionRun")->setEnabled(controller_->canRun() || controller_->running());

    QString saveReason = controller_->saveBlockedReason();
    QString runReason = controller_->runBlockedReason();
    QString busyReason = controller_->running() ? "The pipeline is running" : controller_->updating() ? "Validation is running" : QString();
    setReason(act("actionNew"), "New", busyReason);
    setReason(act("actionOpen"), "Open", busyReason);
    setReason(act("actionSave"), "Save", saveReason);
    setReason(act("actionSaveAs"), "Save as", saveReason);
    setReason(act("actionRun"), controller_->running() ? "Stop" : "Run", controller_->running() ? QString() : runReason);
    act("actionUpdate")->setToolTip(!hasPlan ? "Validate is off. The canvas is empty" : busy ? "Validate is off. Wait for the running step to finish" : "Validate the pipeline (Ctrl+R)");
    act("actionAddElement")->setToolTip(blocked ? "Add element is off. GStreamer is not ready" : editBlocked ? "Add element is off. Wait for the running step to finish" : "Add an element: a source, a filter or a sink (Ctrl+E)");

    const UpdateResult& r = controller_->result();
    QString text;
    QString colour = Theme::text().name();
    if (blocked) text = "GStreamer is not ready";
    else if (controller_->updating()) text = "Validating the latest changes… You can keep editing";
    else if (controller_->running()) text = "Running…";
    else if (controller_->monitoring()) text = "Monitoring live metrics…";
    else if (!hasPlan) text = "Empty canvas: choose Add source to start";
    else if (controller_->notValidated()) text = "Validation was cancelled — press Validate. Run is off", colour = Theme::warning().name();
    else if (r.valid() && !r.ok()) text = "Validation reported a problem. Run is off", colour = Theme::error().name();
    else if (r.valid() && !r.complete()) text = "Pipeline is not complete (free src pad). Run is off", colour = Theme::warning().name();
    else if (r.valid()) text = "Pipeline is complete", colour = Theme::accent().name();
    else text = "Not validated yet — press Validate";
    state_->setText(text);
    state_->setStyleSheet("color: " + colour + ";");
    const StartupResult& env = services_.environment().result();
    environment_->setText(env.state == StartupResult::Ready ? env.statusText() : QString());
    command_->setText(controller_->launchCommand());

    scene_->setLocked(editBlocked);
    scene_->setStale(controller_->notValidated());
    view_->setDimmed(controller_->running());
    drawer_->setBusy(editBlocked);
    flows_->setBusy(editBlocked);

    setWindowTitle(BuildInfo::title() + " " + BuildInfo::version() + " – " + doc_->title() + (controller_->hasUnsavedWork() ? " *" : ""));
}

void MainWindow::onEnvironment() {
    const StartupResult& env = services_.environment().result();
    if (env.bad()) {
        setup_->show(env);
        centre_->setCurrentWidget(setup_);
        drawer_->dismiss();
        flows_->closeProperties();
        flows_->closeCaps();
    } else {
        centre_->setCurrentIndex(0);
    }
    refreshState();
}

void MainWindow::openFile(const QString& path) {
    Settings::addRecent(path);
    open(std::make_unique<ProjectConfTarget>(path));
}

// A pristine tab is a blank to fill, even when it reads the same Untitled-N as a backup to reopen.
Document* MainWindow::open(std::unique_ptr<SaveTarget> target, const QString& backup) {
    for (Document* d : std::as_const(docs_)) {
        if (d->shows(target->sessionKey()) && !d->pristine()) {
            activate(d);
            return d;
        }
    }
    Document* d = blankTab();
    d->opening(target->title());
    if (!backup.isEmpty()) d->backup()->continueFrom(backup);
    d->controller()->open(std::move(target), BackupService::contentOf(backup));
    refreshTab(d);
    return d;
}

void MainWindow::toggleRun() {
    if (controller_->running()) controller_->stopRun();
    else controller_->run();
}

void MainWindow::openPropertiesTab(const QString& element) {
    flows_->editProperties(element);
}

void MainWindow::resetPropertiesTab() {
    flows_->closeProperties();
}

void MainWindow::resetCapsTab() {
    flows_->closeCaps();
}

void MainWindow::openCapsTab(const QString& srcElement, const QString& srcIdentity) {
    flows_->editCaps(srcElement, srcIdentity);
}

// The documents go first, while the docks and stacks that show their pages still stand.
MainWindow::~MainWindow() {
    const QList<Document*> documents = std::exchange(docs_, {});
    doc_ = nullptr;
    qDeleteAll(documents);
}

void MainWindow::showCanvasMenu(const QPoint& screenPos) {
    if (controller_->editingBlocked() || services_.environment().blocked()) return;
    auto* menu = new QMenu(this);
    menu->setObjectName("canvasMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction("Add element…", this, [this] { flows_->chooseElement(); })->setObjectName("actionCanvasAddElement");
    menu->addAction("Add template…", this, [this] { flows_->chooseTemplate(); })->setObjectName("actionCanvasAddTemplate");
    QStringList sel = scene_->selectedElementNames();
    if (!sel.isEmpty()) {
        menu->addSeparator();
        menu->addAction(QString("Save selection (%1) as template…").arg(sel.size()), this, [this] { saveSelectedAsTemplate(); })
            ->setObjectName("actionCanvasSaveCustom");
        menu->addAction("Auto align", this, [this, sel] { controller_->niceAlign(sel); })->setObjectName("actionCanvasAutoAlign");
        if (sel.size() >= 2) {
            menu->addAction("Connect selected elements", this, [this] { connectSelected(); })->setObjectName("actionCanvasConnectSelected");
        }
        menu->addAction("Duplicate selected", this, [this] { duplicateSelected(); });
        menu->addAction("Copy selected", this, [this] { copySelected(); });
        menu->addAction("Cut selected", this, [this] { cutSelected(); });
        menu->addAction("Delete selected", this, [this] { deleteSelected(); });
    }
    menu->addSeparator();
    auto* u = menu->addAction("Undo", this, [this] { controller_->undo(); });
    u->setEnabled(controller_->canUndo());
    auto* r = menu->addAction("Redo", this, [this] { controller_->redo(); });
    r->setEnabled(controller_->canRedo());
    auto* p = menu->addAction("Paste", this, [this] { pasteClipboard(); });
    p->setEnabled(QGuiApplication::clipboard() && !QGuiApplication::clipboard()->text().isEmpty());
    menu->addSeparator();
    menu->addAction("Select all", this, [this] { selectAll(); });
    menu->addAction("Zoom to fit", this, [this] { view_->fitAll(); });
    menu->addAction("Arrange pipeline", this, [this] { controller_->niceAlign(); });
    auto* v = menu->addAction("Validate", this, [this] { controller_->refresh(); });
    v->setEnabled(!controller_->plan().empty());
    menu->popup(screenPos);
}

void MainWindow::onElementMenuRequested(const QString& element, const QPoint& screenPos) {
    if (controller_->editingBlocked()) return;
    QStringList sel = scene_->selectedElementNames();
    QString grp = controller_->templateBox(element);
    if (sel.size() > 1 || !grp.isEmpty()) {
        showSelectionMenu(element, screenPos);
    } else {
        flows_->showElementMenu(element, screenPos);
    }
}

void MainWindow::showTemplateMenu(const QString& instanceId, const QPoint& screenPos) {
    if (controller_->editingBlocked()) return;
    const FrameView frame = controller_->frameView(instanceId);
    if (frame.id.isEmpty()) return;
    auto* menu = new QMenu(this);
    menu->setObjectName("templateMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    addEditTemplate(menu, instanceId);
    menu->addAction("Collapse '" + frame.title + "'", this, [this, instanceId] { controller_->setTemplateExpanded(instanceId, false); })
        ->setObjectName("actionCollapseTemplate");
    menu->addAction("Auto align", this, [this, frame] { controller_->niceAlign(frame.members); })->setObjectName("actionTemplateAutoAlign");
    menu->addAction("Save as template…", this, [this] { saveSelectedAsTemplate(); })->setObjectName("actionTemplateSaveAsTemplate");
    menu->addAction("Detach from template '" + frame.title + "'", this, [this, instanceId] { controller_->detachTemplate(instanceId); })
        ->setObjectName("actionDetachTemplate");
    menu->addSeparator();
    menu->addAction(QString("Delete template (%1 elements)").arg(frame.members.size()), this, [this] { deleteSelected(); })
        ->setObjectName("actionDeleteTemplate");
    menu->popup(screenPos);
}

// "Edit template": the library template of this copy, on a canvas of its own.
void MainWindow::addEditTemplate(QMenu* menu, const QString& instanceId) {
    const gps::TemplateInstance* t = controller_->plan().instance(instanceId.toStdString());
    if (!t) return;
    const QString name = QString::fromStdString(t->templateName);
    QAction* edit = menu->addAction("Edit template '" + name + "'", this, [this, name] { openTemplate(name); });
    edit->setObjectName("actionEditTemplate");
    edit->setEnabled(services_.library().plan(name) != nullptr);
    menu->addSeparator();
}

void MainWindow::showSelectionMenu(const QString& clickedElement, const QPoint& screenPos) {
    if (controller_->editingBlocked()) return;
    QStringList sel = scene_->selectedElementNames();
    if (!sel.contains(clickedElement)) sel << clickedElement;
    sel = controller_->expandSelection(sel);

    auto* menu = new QMenu(this);
    menu->setObjectName("selectionMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);

    QString grp = controller_->templateBox(clickedElement);
    if (!grp.isEmpty()) {
        QString title = grp.section('#', 0, 0);
        bool expanded = controller_->isTemplateExpanded(grp);
        addEditTemplate(menu, grp);
        menu->addAction(expanded ? ("Collapse '" + title + "' to single element") : ("Expand '" + title + "' (" + QString::number(controller_->templateMembers(grp).size()) + " elements)"),
                        this, [this, grp, expanded] { controller_->setTemplateExpanded(grp, !expanded); })
            ->setObjectName("actionToggleGroupExpand");
        menu->addAction("Detach from template '" + title + "'", this, [this, grp] { controller_->detachTemplate(grp); })->setObjectName("actionUngroup");
        if (expanded) {
            menu->addAction("Edit properties of " + clickedElement + "…", this, [this, clickedElement] { flows_->editProperties(clickedElement); });
        }
        menu->addSeparator();
    }

    menu->addAction(QString("Save as template (%1)…").arg(sel.size()), this, [this] { saveSelectedAsTemplate(); })
        ->setObjectName("actionSaveTemplateMenu");
    if (sel.size() >= 2) {
        menu->addSeparator();
        menu->addAction("Connect selected elements", this, [this, sel] { controller_->connectSelectedElements(sel); })
            ->setObjectName("actionConnectSelectedMenu");
        menu->addAction("Disconnect links between selected", this, [this, sel] { controller_->disconnectBetweenElements(sel); })
            ->setObjectName("actionDisconnectSelectedMenu");
    }
    menu->addAction("Auto align", this, [this, sel] { controller_->niceAlign(sel); })->setObjectName("actionAutoAlign");
    menu->addSeparator();
    menu->addAction(QString("Duplicate (%1)").arg(sel.size()), this, [this] { duplicateSelected(); })->setObjectName("actionDuplicateSelection");
    menu->addAction(QString("Copy (%1)").arg(sel.size()), this, [this] { copySelected(); })->setObjectName("actionCopySelection");
    menu->addAction(QString("Cut (%1)").arg(sel.size()), this, [this] { cutSelected(); })->setObjectName("actionCutSelection");
    menu->addAction(QString("Delete (%1)").arg(sel.size()), this, [this] { deleteSelected(); })->setObjectName("actionDeleteSelection");
    menu->popup(screenPos);
}

void MainWindow::saveSelectedAsTemplate() {
    const QStringList selection = scene_->selectedElementNames();
    if (selection.isEmpty()) {
        statusBar()->showMessage("Select elements or a template on the canvas first.", 6000);
        return;
    }
    flows_->saveAsTemplate(selection);
}

void MainWindow::connectSelected() {
    QStringList sel = scene_->selectedElementNames();
    if (sel.size() >= 2) controller_->connectSelectedElements(sel);
}

// Delete on the canvas: a single selected source asks confirmation; multi-selection or non-source elements/links delete atomically.
void MainWindow::deleteSelected() {
    QStringList selectedElements = scene_->selectedElementNames();
    QList<gps::LinkRef> selectedLinks;
    for (QGraphicsItem* item : scene_->selectedItems()) {
        if (auto* link = dynamic_cast<LinkItem*>(item)) selectedLinks.append({link->view().src});
    }
    if (selectedElements.size() == 1 && selectedLinks.isEmpty()) {
        const QString& only = selectedElements.first();
        if (controller_->templateBox(only).isEmpty() && controller_->view(only).isSource) {
            flows_->deleteSource(only);
            return;
        }
    }
    if (!selectedElements.isEmpty() || !selectedLinks.isEmpty()) {
        controller_->deleteSelection(selectedElements, selectedLinks);
    }
}

void MainWindow::duplicateSelected() {
    QStringList names = scene_->selectedElementNames();
    if (names.isEmpty()) return;
    QStringList created = controller_->duplicateElements(names);
    if (!created.isEmpty()) scene_->setSelectedElements(created);
}

void MainWindow::selectAll() {
    for (QGraphicsItem* item : scene_->items()) {
        if (item->flags() & QGraphicsItem::ItemIsSelectable) item->setSelected(true);
    }
}

void MainWindow::copySelected() {
    QStringList names = scene_->selectedElementNames();
    if (names.isEmpty()) return;
    gps::Graph sub = controller_->copySubplan(names);
    if (sub.elements.empty()) return;
    if (QClipboard* cb = QGuiApplication::clipboard()) cb->setText(QString::fromStdString(sub.toConf()));
}

void MainWindow::cutSelected() {
    QStringList names = scene_->selectedElementNames();
    if (names.isEmpty()) return;
    copySelected();
    controller_->deleteSelection(names);
}

void MainWindow::pasteClipboard() {
    if (controller_->editingBlocked()) return;
    QClipboard* cb = QGuiApplication::clipboard();
    if (!cb || cb->text().isEmpty()) return;
    gps::Graph clip;
    std::string err;
    if (!gps::Graph::fromConf(cb->text().toStdString(), &clip, &err)) return;
    QStringList created = controller_->pasteSubplan(clip);
    if (!created.isEmpty()) scene_->setSelectedElements(created);
}

void MainWindow::showAbout() {
    AboutDialog dialog(services_.environment().result().statusText(), this);
    dialog.exec();
}

// "Template changes, what now?" (TEMPLATE_PLAN D17, D30): the change, the projects the template is used in, and four
// answers. Closing the window is Undo: nothing was decided.
void MainWindow::askTemplateChange(const QString& id) {
    const gps::TemplateInstance* t = controller_->plan().instance(id.toStdString());
    if (!t) return;
    const QString name = QString::fromStdString(t->templateName);
    QStringList used;
    for (const QString& project : services_.library().projectsUsing(name)) used << QFileInfo(project).absolutePath();
    QMessageBox box(this);
    box.setObjectName("templateChange");
    box.setWindowTitle("Template changes");
    box.setIcon(QMessageBox::Question);
    box.setText(QString("<b>'%1' template changes</b>").arg(name.toHtmlEscaped()));
    box.setInformativeText("Change: " + controller_->lastAction() + "\n\n" +
                           (used.isEmpty() ? QString("No saved project uses this template yet.")
                                           : "Used in:\n  " + used.join("\n  ") + "\n\nUpdating it changes these projects too."));
    QPushButton* update = box.addButton("Update template", QMessageBox::AcceptRole);
    QPushButton* asNew = box.addButton("Save as new template…", QMessageBox::ActionRole);
    QPushButton* detach = box.addButton("Detach", QMessageBox::ActionRole);
    QPushButton* undo = box.addButton("Undo", QMessageBox::RejectRole);
    update->setObjectName("updateTemplate");
    asNew->setObjectName("saveAsNewTemplate");
    detach->setObjectName("detachTemplate");
    undo->setObjectName("undoTemplateChange");
    box.setDefaultButton(update);
    box.setEscapeButton(undo);
    box.exec();
    if (box.clickedButton() == update) {
        controller_->keepEditingTemplate(id);
        statusBar()->showMessage("Editing template " + name + ": the change goes to the library when it collapses or the file is saved.", 8000);
    } else if (box.clickedButton() == asNew) {
        bool ok = false;
        const QString newName = QInputDialog::getText(this, "Save as new template", "Name of the new template:", QLineEdit::Normal, name + " 2", &ok);
        QString error;
        if (!ok || !controller_->saveTemplateAsNew(id, newName, &error)) {
            if (!error.isEmpty()) QMessageBox::warning(this, "Save as new template", error);
            controller_->undo();
        }
    } else if (box.clickedButton() == detach) {
        controller_->detachTemplate(id);
    } else {
        controller_->undo();
    }
}

// An opened file holds older versions of templates the library has newer ones of.
void MainWindow::askTemplatesOutdated(const QStringList& ids) {
    QStringList lines;
    for (const QString& id : ids) {
        const gps::TemplateInstance* t = controller_->plan().instance(id.toStdString());
        if (!t) continue;
        const QString name = QString::fromStdString(t->templateName);
        lines << QString("%1: revision %2 → %3%4").arg(name).arg(t->revision).arg(services_.library().content(name).revision)
                     .arg(gps::templates::modified(controller_->plan(), t->id) ? "  (its changes here would be lost)" : "");
    }
    if (lines.isEmpty()) return;
    const auto answer = QMessageBox::question(this, "Templates were updated",
        "The library has newer versions of templates this file uses:\n\n  " + lines.join("\n  ") + "\n\nUse the newer versions?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes) controller_->updateTemplates(ids);
}

// The earlier versions of the open file, newest first; the chosen one comes back as an unsaved change.
void MainWindow::showVersionHistory() {
    const QString file = doc_->path();
    const QList<VersionHistory::Version> versions = VersionHistory::versions(file);
    if (file.isEmpty() || versions.isEmpty()) return;
    QDialog dialog(this);
    dialog.setObjectName("versionHistory");
    dialog.setWindowTitle("Version history · " + QFileInfo(file).fileName());
    auto* layout = new QVBoxLayout(&dialog);
    auto* hint = new QLabel(QString("Every save keeps the file as it was (the newest %1). Restoring one replaces the canvas; "
                                    "save to keep it.").arg(VersionHistory::kKept), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto* list = new QListWidget(&dialog);
    list->setObjectName("versionList");
    for (const VersionHistory::Version& v : versions) {
        auto* item = new QListWidgetItem(QLocale().toString(v.saved, QLocale::LongFormat), list);
        item->setData(Qt::UserRole, v.path);
    }
    list->setCurrentRow(0);
    layout->addWidget(list, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* restore = buttons->addButton("Restore", QDialogButtonBox::AcceptRole);
    restore->setObjectName("restoreVersion");
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    layout->addWidget(buttons);
    dialog.resize(460, 380);
    if (dialog.exec() != QDialog::Accepted || !list->currentItem() || !saveFlow_->confirmClose({doc_})) return;
    controller_->restoreVersion(list->currentItem()->data(Qt::UserRole).toString());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    for (Document* d : std::as_const(docs_)) {
        if (d->controller()->running()) d->controller()->stopRun();
        d->backup()->flush();
    }
    saveSession();
    Settings::setWindow(saveGeometry(), saveState());
    event->accept();
}

Document* MainWindow::openTemplate(const QString& name) {
    if (!services_.library().plan(name)) {
        statusBar()->showMessage("Template " + name + " is not in the library", 10000);
        return nullptr;
    }
    return open(std::make_unique<LibraryTemplateTarget>(name, services_.library()));
}
