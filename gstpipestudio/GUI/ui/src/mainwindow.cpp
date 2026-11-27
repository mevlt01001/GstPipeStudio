// mainwindow.cpp – see mainwindow.h.
#include "mainwindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenuBar>
#include <QPointer>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <tuple>

#include "aboutdialog.h"
#include "onboardingdialog.h"
#include "appservices.h"
#include "buildinfo.h"
#include "canvasscene.h"
#include "canvasview.h"
#include "contextmenus.h"
#include "controller.h"
#include "pipelinecompiler.h"
#include "document.h"
#include "documentbackup.h"
#include "documenttabs.h"
#include "documentsession.h"
#include "drawer.h"
#include "explorer.h"
#include "explorersource.h"
#include "fileoperations.h"
#include "logpanel.h"
#include "mcpbridge.h"
#include "mcpinspectorpanel.h"
#include "flows.h"
#include "notificationoverlay.h"
#include "panels.h"
#include "pipelinefiledialog.h"
#include "prompt.h"
#include "propertypage.h"
#include "saveflow.h"
#include "savetarget.h"
#include "settings.h"
#include "setuppage.h"
#include "telemetrypanel.h"
#include "templates.h"
#include "theme.h"
#include "updatepanel.h"
#include "validationindicator.h"
#include "versionhistory.h"
#include "windowchrome.h"

MainWindow::MainWindow(DocumentSessions& sessions, QWidget* parent)
    : MainWindow(sessions.services(), &sessions, nullptr, parent) {}

MainWindow::MainWindow(Controller* controller, QWidget* parent)
    : MainWindow(controller->services(), nullptr, controller, parent) {}

MainWindow::MainWindow(AppServices& services, DocumentSessions* sessions, Controller* first, QWidget* parent)
    : QMainWindow(parent), services_(services), ownedSessions_(sessions ? nullptr : std::make_unique<DocumentSessions>(services)),
      saveFlow_(new SaveFlow(this)), registry_(new DocumentRegistry(this)), files_(new FileOperations(services_, this)),
      tabs_(new DocumentTabs(sessions ? *sessions : *ownedSessions_, this)), commands_(new CommandSet(this)),
      menus_(new ContextMenus(*commands_, this)) {
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
    column->addWidget(tabs_->bar());
    column->addWidget(tabs_->pages(), 1);
    setCentralWidget(centre);
    connect(tabs_, &DocumentTabs::added, this, &MainWindow::added);
    connect(tabs_, &DocumentTabs::activated, this, &MainWindow::activated);
    connect(tabs_, &DocumentTabs::closeRequested, this, &MainWindow::closeDocument);
    connect(tabs_, &DocumentTabs::menuRequested, menus_, &ContextMenus::tab);
    connect(registry_, &DocumentRegistry::changedOnDisk, this, [this](Document* d) { onChangedOnDisk(d, DiskChange::External); });
    connect(registry_, &DocumentRegistry::deletedOnDisk, this, [this](Document* d) {
        notify({NoticeSeverity::Warning, "File deleted on disk", d->controller()->target().title() + " was deleted on disk: save to keep it", false});
    });
    // After the writing tab has taken its save: only the other tabs of that template are told.
    connect(&services_.library(), &TemplateLibrary::changed, this, &MainWindow::onLibraryChanged, Qt::QueuedConnection);

    panels_ = new Panels(this);
    notifications_ = new NotificationOverlay(this);
    connect(notifications_, &NotificationOverlay::viewLogRequested, this, [this] { panels_->show(logPanel()); });
    buildStatusBar();
    tabs_->add(first);
    mcpPanel_ = new McpInspectorPanel(services_.mcpAudit(), this);
    panels_->add(mcpPanel_, "MCP", 0, Qt::BottomDockWidgetArea);
    mcpLabel_->setText(QString("MCP: %1").arg(mcpPanel_->entryCount()));
    connect(mcpPanel_, &McpInspectorPanel::countChanged, this, [this](int count) {
        if (mcpLabel_) mcpLabel_->setText(QString("MCP: %1").arg(count));
    });
    connect(mcpPanel_, &McpInspectorPanel::configCopied, this, [this] {
        notify({NoticeSeverity::Info, "MCP Config Copied", "Copied gps-mcp configuration JSON to clipboard.", false});
    });
    mcpBridge_ = new McpBridgeServer(services_, services_.mcpAudit(), [this] { return front() ? front()->controller() : nullptr; }, this);
    if (!qEnvironmentVariableIsSet("GPS_TEST_MODE")) mcpBridge_->start();
    explorer_ = new Explorer(ExplorerSource::standard(services_, this), *files_, this);
    panels_->add(explorer_, "Explorer", 0, Qt::LeftDockWidgetArea);
    resizeDocks({panels_->dock(logPanel())}, {240}, Qt::Vertical);
    resizeDocks({panels_->dock(explorer_)}, {260}, Qt::Horizontal);
    panels_->show(updatePanel());

    buildCommands();
    buildMenus();
    setMenuWidget(new WindowChrome(this, menuBar()));
    restoreGeometry(Settings::windowGeometry());
    restoreState(Settings::windowState());
    built_ = true;
    connect(&services_.view(), &ViewPreferences::changed, this, &MainWindow::scheduleRefresh);
    Theme::onChanged(this, [this] { refreshState(); });
    if (QClipboard* clipboard = QGuiApplication::clipboard()) connect(clipboard, &QClipboard::dataChanged, this, &MainWindow::scheduleRefresh);
    // GStreamer is checked once for the whole window; every tab shows the answer.
    connect(&services_.environment(), &EnvironmentService::checked, this, &MainWindow::onEnvironment);
    services_.environment().check();
    activate(front());
    if (!Settings::onboardingCompleted() && QGuiApplication::platformName() != "offscreen" &&
        !qEnvironmentVariableIsSet("GPS_TEST_MODE")) {
        QTimer::singleShot(0, this, &MainWindow::showWelcomeTour);
    }
}

// The documents go first, while the docks and stacks that show their pages still stand.
MainWindow::~MainWindow() { delete tabs_; }

// ---- the front document

Document* MainWindow::front() const { return tabs_->front(); }
Controller* MainWindow::controller() const { return front()->controller(); }
QList<Document*> MainWindow::documents() const { return tabs_->all(); }
QTabBar* MainWindow::tabs() const { return tabs_->bar(); }
CanvasScene* MainWindow::scene() const { return front()->scene(); }
CanvasView* MainWindow::view() const { return front()->view(); }
Flows* MainWindow::flows() const { return front()->flows(); }
Drawer* MainWindow::drawer() const { return front()->drawer(); }
UpdatePanel* MainWindow::updatePanel() const { return front()->updates(); }
LogPanel* MainWindow::logPanel() const { return front()->log(); }
QWidget* MainWindow::propertiesTab() const { return front()->propsTab(); }
QWidget* MainWindow::capsTab() const { return front()->capsTab(); }
SetupPage* MainWindow::setupPage() const { return front()->setup(); }
ValidationIndicator* MainWindow::indicator() const { return front()->indicator(); }
bool MainWindow::showingSetup() const { return front()->centre()->currentWidget() == front()->setup(); }

// ---- commands

namespace {
// The first reason a command is off: a general one (busy, not editable) before the command's own.
QString firstOf(const QString& general, const QString& own) { return general.isEmpty() ? own : general; }
}  // namespace

QString MainWindow::environmentReason() const { return services_.environment().blocked() ? "GStreamer is not ready" : QString(); }

QString MainWindow::busyReason() const { return firstOf(environmentReason(), controller()->busyReason()); }

QString MainWindow::editReason() const { return controller()->saveBlockedReason(); }

QString MainWindow::addReason() const { return firstOf(environmentReason(), editReason()); }

QString MainWindow::selectionReason(int least) const {
    const QString own = front()->selection().size() >= least ? QString() : least == 1 ? "Nothing is selected" : "Select two elements or more";
    return firstOf(editReason(), own);
}

QString MainWindow::copyTitle() const { return fromStd(controller()->names().instance(toStd(commands_->target().instance))); }

// The one table of commands: what each does, and why it is off.
void MainWindow::buildCommands() {
    using C = Command;
    auto environment = [this] { return environmentReason(); };  // a new tab never waits for the front one (DocumentTabs::target)
    auto edit = [this] { return editReason(); };
    auto add = [this] { return addReason(); };
    auto selected = [this] { return selectionReason(); };
    auto pair = [this] { return selectionReason(2); };
    auto withPlan = [this] { return firstOf(editReason(), controller()->plan().empty() ? "The canvas is empty" : QString()); };
    auto target = [this] { return commands_->target(); };
    commands_->add({
        {C::New, "actionNew", "&New", {QKeySequence::New}, [this] { if (!front()->pristine()) newDocument(); }, environment},
        {C::NewProject, "actionNewProject", "New &project…", {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N)}, [this] { files_->newProject(); }},
        {C::Open, "actionOpen", "&Open…", {QKeySequence::Open}, [this] {
             if (const QString path = PipelineFileDialog::choose(this, false, Settings::lastDir()); !path.isEmpty()) openFile(path);
         }, environment},
        {C::Save, "actionSave", "&Save", {QKeySequence::Save}, [this] { saveFlow_->save(front()); }, edit},
        {C::SaveAs, "actionSaveAs", "Save &as…", {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)}, [this] { saveFlow_->saveAs(front()); }, [this] {
             return firstOf(editReason(), controller()->target().saveAsBlockedReason());
         }},
        {C::SaveAll, "actionSaveAll", "Save a&ll", {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S)}, [this] { saveFlow_->saveAll(documents()); }, [this] {
             const QList<Document*> all = documents();
             return std::any_of(all.cbegin(), all.cend(), [](const Document* d) { return d->controller()->hasUnsavedWork(); }) ? QString() : "Nothing is unsaved";
         }},
        {C::Revert, "actionRevert", "Re&vert file", {}, [this] { controller()->revert(); }, [this] { return controller()->canRevert() ? QString() : "Nothing to revert"; }},
        {C::CloseTab, "actionCloseTab", "&Close tab", {QKeySequence(Qt::CTRL | Qt::Key_W), QKeySequence(Qt::CTRL | Qt::Key_F4)}, [this] { closeDocument(front()); }},
        {C::NewPlugin, "actionNewPlugin", "New Custom &Plugin…", {}, [this] { files_->newPlugin(); }, environment},
        {C::ImportLaunch, "actionImportLaunch", "New from &gst-launch…", {}, [this] { importLaunch(); }, environment},
        {C::ExportSharedLibrary, "actionExportSharedLibrary", "Export Shared &Library (.so)…", {}, [this] { exportSharedLibrary(); },
         [this] { return firstOf(busyReason(), controller()->plan().empty() ? "The canvas is empty" : QString()); }},
        {C::VersionHistory, "actionVersionHistory", "Version &history…", {}, [this] { showVersionHistory(); }, [this] {
             return firstOf(busyReason(), VersionHistory::versions(front()->path()).isEmpty() ? "No earlier versions" : QString());
         }},
        {C::Quit, "actionQuit", "&Quit", {QKeySequence::Quit}, [this] { close(); }},

        {C::Undo, "actionUndo", "&Undo", {QKeySequence::Undo}, [this] { controller()->undo(); }, [this] { return controller()->canUndo() ? QString() : "Nothing to undo"; }},
        {C::Redo, "actionRedo", "&Redo", {QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)}, [this] { controller()->redo(); },
         [this] { return controller()->canRedo() ? QString() : "Nothing to redo"; }},
        {C::Cut, "actionCut", "Cu&t", {QKeySequence::Cut}, [this] { front()->cutSelection(); }, selected},
        {C::Copy, "actionCopy", "&Copy", {QKeySequence::Copy}, [this] { front()->copySelection(); }, [this] { return front()->selection().isEmpty() ? "Nothing is selected" : QString(); }},
        {C::Paste, "actionPaste", "&Paste", {QKeySequence::Paste}, [this] { front()->paste(); }, [this] {
             const QClipboard* clipboard = QGuiApplication::clipboard();
             return firstOf(editReason(), !clipboard || clipboard->text().isEmpty() ? "The clipboard is empty" : QString());
         }},
        {C::Duplicate, "actionDuplicate", "&Duplicate", {QKeySequence(Qt::CTRL | Qt::Key_D)}, [this] { front()->duplicateSelection(); }, selected},
        {C::Delete, "actionDelete", "De&lete", {}, [this] { front()->deleteSelection(); }, [this] {
             return firstOf(editReason(), front()->hasSelectedItems() ? QString() : "Nothing is selected");
         }},
        {C::SelectAll, "actionSelectAll", "Select &All", {QKeySequence::SelectAll}, [this] { front()->selectAll(); }},
        {C::Find, "actionFind", "&Find element", {QKeySequence::Find}, [this] { focusSearch(); }},

        {C::NextTab, "actionNextTab", "&Next tab", {QKeySequence(Qt::CTRL | Qt::Key_Tab), QKeySequence(Qt::CTRL | Qt::Key_PageDown)}, [this] { tabs_->step(1); }},
        {C::PreviousTab, "actionPreviousTab", "Pre&vious tab", {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab), QKeySequence(Qt::CTRL | Qt::Key_PageUp)},
         [this] { tabs_->step(-1); }},
        {C::ZoomIn, "actionZoomIn", "Zoom &in", {QKeySequence::ZoomIn}, [this] { view()->zoomBy(1.25); }},
        {C::ZoomOut, "actionZoomOut", "Zoom &out", {QKeySequence::ZoomOut}, [this] { view()->zoomBy(0.8); }},
        {C::Fit, "actionFit", "Zoom to &fit", {QKeySequence(Qt::CTRL | Qt::Key_0)}, [this] { view()->fitAll(); }},
        {C::Arrange, "actionArrange", "Arrange &pipeline", {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L)}, [this] { controller()->niceAlign(); }, withPlan},
        // The view preferences are the process's: the menu sets them once, every tab follows.
        {C::LightTheme, "actionLightTheme", "&Light theme", {}, [] {
             const bool light = Theme::mode() != Theme::Mode::Light;
             Settings::setLightTheme(light);
             Theme::setMode(*qApp, light ? Theme::Mode::Light : Theme::Mode::Dark);
         }},
        {C::AutoAlignment, "actionAutoAlignment", "&Auto alignment", {}, [this] {
             services_.view().setAutoAlign(!services_.view().autoAlign());
             if (services_.view().autoAlign()) controller()->niceAlign();
         }},
        {C::LinkCurves, "actionLinkCurves", "&Curves", {}, [this] { services_.view().setRouting(layout::Routing::Curves); controller()->niceAlign(); }},
        {C::LinkRightAngles, "actionLinkRightAngles", "&Right angles", {}, [this] { services_.view().setRouting(layout::Routing::RightAngles); controller()->niceAlign(); }},
        {C::ConnectedMove, "actionConnectedMove", "Move connected elements together", {}, [this] { services_.view().setConnectedMove(!services_.view().connectedMove()); }},

        {C::AddElement, "actionAddElement", "Add &element…", {QKeySequence(Qt::CTRL | Qt::Key_E), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)},
         [this] { flows()->chooseElement(); }, add},
        {C::AddTemplate, "actionAddTemplate", "Add &template…", {QKeySequence(Qt::CTRL | Qt::Key_T)}, [this] { flows()->chooseComponent(); }, add},
        {C::AddPlugin, "actionAddPlugin", "Custom plugin…", {}, [this] {
             if (services_.plugins().components().isEmpty()) files_->newPlugin();
             else flows()->chooseComponent(services_.plugins().kind());
         }, add},
        {C::AddLaunch, "actionAddLaunch", "From &gst-launch…", {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I)}, [this] { addLaunch(); }, add},
        {C::SaveAsTemplate, "actionSaveTemplateElement", "Save as &template…", {QKeySequence(Qt::CTRL | Qt::Key_G)}, [this] { flows()->saveAsTemplate(front()->selection()); }, selected},
        {C::TemplateParameters, "actionTemplateParameters", "Template &parameters…", {}, [this] { flows()->editTemplateParameters(); }, [this] {
             return firstOf(editReason(), controller()->target().libraryTemplate().isEmpty() ? "Only a library template has parameters" : QString());
         }},
        {C::ConnectSelected, "actionConnectSelected", "Connect &selected", {QKeySequence(Qt::CTRL | Qt::Key_L)}, [this] { controller()->connectSelectedElements(front()->selection()); }, pair},
        {C::DisconnectSelected, "actionDisconnectSelected", "Disconnect selected", {}, [this] { controller()->disconnectBetweenElements(front()->selection()); }, pair},
        {C::AlignSelection, "actionAlignSelection", "Auto align", {}, [this] { front()->alignSelection(); }, selected},
        {C::Validate, "actionUpdate", "&Validate", {QKeySequence(Qt::CTRL | Qt::Key_R), QKeySequence(Qt::Key_F5)}, [this] { controller()->refresh(); }, [this] {
             return firstOf(busyReason(), controller()->plan().empty() ? "The canvas is empty" : QString());
         }},
        {C::Run, "actionRun", "&Run", {QKeySequence(Qt::CTRL | Qt::Key_Return)}, [this] { controller()->running() ? controller()->stopRun() : (void)controller()->run(); },
         [this] { return controller()->running() ? QString() : firstOf(environmentReason(), controller()->runBlockedReason()); }},

        {C::WelcomeTour, "actionWelcomeTour", "&Welcome & Quick Tour…", {}, [this] { showWelcomeTour(); }},
        {C::GstInfo, "actionGstInfo", "&GStreamer information", {}, [this] {
             const StartupResult& env = services_.environment().result();
             Prompt::inform(this, "GStreamer information", env.version.isEmpty() ? "GStreamer was not checked or not found." : env.statusText());
         }},
        {C::Support, "actionSupport", "&Request support…", {}, [this] { controller()->requestSupport("Problem report"); }},
        {C::About, "actionAbout", "&About GstPipeStudio", {}, [this] { showAbout(); }},

        {C::EditProperties, "actionProperties", "Edit properties…", {}, [this, target] { flows()->editProperties(target().element); }},
        {C::RemoveQueue, "actionRemoveQueue", "Remove queue", {}, [this, target] { controller()->removeQueue(target().element); }},
        {C::EditCaps, "actionEditCaps", "Edit caps…", {}, [this, target] { flows()->editCaps(target().element, target().pad); }},
        {C::ReplaceBehind, "actionReplace", "Replace element…", {}, [this, target] { flows()->replaceBehind(target().element, target().pad); }},
        {C::CutLink, "actionCutLink", "Cut link", {}, [this, target] { controller()->cut(target().element, target().pad); }},
        {C::InsertQueue, "actionInsertQueue", "Insert queue", {}, [this, target] { flows()->addQueue(target().element, target().pad); }},
        {C::EditTemplate, "actionEditTemplate", "Edit template", {}, [this, target] { openTemplate(controller()->templateOf(target().instance)); }, [this, target] {
             return services_.library().plan(controller()->templateOf(target().instance)) ? QString() : "The template is not in the library";
         }},
        {C::ToggleTemplate, "actionToggleTemplate", "Collapse", {}, [this, target] {
             controller()->setTemplateExpanded(target().instance, !controller()->isTemplateExpanded(target().instance));
         }},
        {C::DetachTemplate, "actionDetachTemplate", "Detach from template", {}, [this, target] { controller()->detachTemplate(target().instance); }},
    });
    commands_->setLabel(C::Run, [this] { return controller()->running() ? "&Stop" : "&Run"; });
    commands_->setLabel(C::EditTemplate, [this, target] { return "Edit template '" + controller()->templateOf(target().instance) + "'"; });
    commands_->setLabel(C::DetachTemplate, [this] { return "Detach from template '" + copyTitle() + "'"; });
    commands_->setLabel(C::ToggleTemplate, [this, target] {
        const QString id = target().instance;
        return controller()->isTemplateExpanded(id) ? "Collapse '" + copyTitle() + "'"
                                                     : QString("Expand '%1' (%2 elements)").arg(copyTitle()).arg(controller()->outline().membersOf(id).size());
    });
    commands_->setTip(C::Delete, "Delete the selected items (Delete on the canvas)");
    commands_->setTip(C::Validate, "Validate the pipeline (Ctrl+R)");
    commands_->setTip(C::AddElement, "Add an element: a source, a filter or a sink (Ctrl+E)");
    commands_->setTip(C::AddLaunch, "Add elements from a gst-launch pipeline into the current project (Ctrl+Shift+I)");
    commands_->setTip(C::AutoAlignment, "Align the whole canvas after every edit; a dragged box slides back into place");
    commands_->setTip(C::ConnectedMove, "Drag an element to move its connected component; undo restores all positions");
    commands_->setChecked(C::LightTheme, [] { return Theme::mode() == Theme::Mode::Light; });
    commands_->setChecked(C::AutoAlignment, [this] { return services_.view().autoAlign(); });
    commands_->setChecked(C::LinkCurves, [this] { return services_.view().routing() == layout::Routing::Curves; });
    commands_->setChecked(C::LinkRightAngles, [this] { return services_.view().routing() == layout::Routing::RightAngles; });
    commands_->setChecked(C::ConnectedMove, [this] { return services_.view().connectedMove(); });
    for (auto [id, words] : {std::pair{C::AddElement, "Add element"}, {C::AddTemplate, "Add template"}, {C::AddPlugin, "Add plugin"}, {C::AddLaunch, "Add gst-launch"},
                             {C::Undo, "Undo"}, {C::Redo, "Redo"}, {C::Delete, "Delete"}, {C::SaveAsTemplate, "Save as template"}})
        (*commands_)[id]->setIconText(words);
    commands_->refresh();
}

// The menus as desktop applications have them: File, Edit, View, then the application's own, then Help.
void MainWindow::buildMenus() {
    using C = Command;
    CommandSet& c = *commands_;
    QMenu* file = menuBar()->addMenu("&File");
    file->addActions({c[C::NewProject], c[C::New], c[C::NewPlugin], c[C::Open]});
    QMenu* recent = file->addMenu("Open &recent");
    recent->setObjectName("menuRecent");
    connect(recent, &QMenu::aboutToShow, this, [this, recent] {
        recent->clear();
        const QStringList files = Settings::recentFiles();
        for (const QString& path : files) recent->addAction(QFileInfo(path).fileName(), this, [this, path] { openFile(path); })->setToolTip(path);
        if (files.isEmpty()) recent->addAction("No recent files")->setEnabled(false);
    });
    file->addSeparator();
    file->addActions({c[C::Save], c[C::SaveAs], c[C::SaveAll]});
    file->addSeparator();
    file->addActions({c[C::Revert], c[C::CloseTab]});
    file->addSeparator();
    file->addActions({c[C::ImportLaunch], c[C::ExportSharedLibrary], c[C::VersionHistory]});
    file->addSeparator();
    file->addAction(c[C::Quit]);

    QMenu* edit = menuBar()->addMenu("&Edit");
    edit->addActions({c[C::Undo], c[C::Redo]});
    edit->addSeparator();
    edit->addActions({c[C::Cut], c[C::Copy], c[C::Paste], c[C::Duplicate], c[C::Delete]});
    edit->addSeparator();
    edit->addActions({c[C::SelectAll], c[C::Find]});

    QMenu* viewMenu = menuBar()->addMenu("&View");
    QMenu* panels = viewMenu->addMenu("&Panels");
    panels->addActions(panels_->toggles());
    panels->addSeparator();
    panels->addAction("&Reset layout", this, [this] { panels_->reset(updatePanel()); })->setObjectName("actionResetLayout");
    viewMenu->addActions({c[C::NextTab], c[C::PreviousTab]});
    viewMenu->addSeparator();
    viewMenu->addActions({c[C::ZoomIn], c[C::ZoomOut], c[C::Fit]});
    viewMenu->addSeparator();
    viewMenu->addActions({c[C::Arrange], c[C::AutoAlignment]});
    viewMenu->addAction(c[C::LightTheme]);
    QMenu* linkStyle = viewMenu->addMenu("&Link style");  // the next nice alignment routes links as curves or right-angle lines
    auto* styles = new QActionGroup(this);
    for (Command style : {C::LinkCurves, C::LinkRightAngles}) linkStyle->addAction(styles->addAction(c[style]));
    viewMenu->addAction(c[C::ConnectedMove]);

    QMenu* pipeline = menuBar()->addMenu("&Pipeline");
    pipeline->addActions({c[C::AddElement], c[C::AddTemplate], c[C::AddPlugin], c[C::AddLaunch], c[C::SaveAsTemplate], c[C::TemplateParameters], c[C::ConnectSelected]});
    pipeline->addSeparator();
    pipeline->addActions({c[C::Validate], c[C::Run]});

    QMenu* help = menuBar()->addMenu("&Help");
    help->addActions({c[C::WelcomeTour], c[C::GstInfo], c[C::Support]});
    help->addSeparator();
    help->addAction(c[C::About]);

    QToolBar* bar = addToolBar("Tools");
    bar->setObjectName("toolbar");
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bar->addActions({c[C::New], c[C::Open], c[C::Save], c[C::SaveAs]});
    bar->addSeparator();
    bar->addActions({c[C::Undo], c[C::Redo]});
    bar->addSeparator();
    bar->addActions({c[C::Validate], c[C::Run]});
    bar->addSeparator();
    bar->addActions({c[C::AddElement], c[C::AddTemplate], c[C::AddPlugin], c[C::AddLaunch], c[C::SaveAsTemplate]});
    bar->addSeparator();
    bar->addAction(c[C::Delete]);
    bar->widgetForAction(c[C::Run])->setObjectName("primaryAction");
}

void MainWindow::buildStatusBar() {
    state_ = new QLabel(this);
    state_->setObjectName("stateLabel");
    state_->setMinimumWidth(1);
    environment_ = new QLabel(this);
    environment_->setObjectName("environmentLabel");
    environment_->setMinimumWidth(1);
    Theme::setStyle(environment_, "color: @muted@;");
    mcpLabel_ = new QLabel("MCP: 0", this);
    mcpLabel_->setObjectName("mcpLabel");
    mcpLabel_->setMinimumWidth(1);
    Theme::setStyle(mcpLabel_, "color: @muted@;");
    command_ = new QLineEdit(this);
    command_->setObjectName("commandLabel");
    command_->setAccessibleName("Pipeline launch command");
    command_->setReadOnly(true);
    command_->setFrame(false);
    command_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    Theme::setStyle(command_, "font-family: 'DejaVu Sans Mono'; background: transparent; border: 0; color: @muted@;");
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(state_);
    statusBar()->addWidget(environment_);
    statusBar()->addWidget(mcpLabel_);
    statusBar()->addPermanentWidget(command_, 1);
}

// ---- documents

void MainWindow::added(Document* d) {
    for (auto [page, title, room] : {std::tuple{static_cast<QWidget*>(d->log()), "Log", 0}, {d->updates(), "Validation", 0},
                                     {d->telemetry(), "Live metrics", 400}, {d->propsTab(), "Properties", 400}, {d->capsTab(), "Caps", 400}})
        panels_->add(page, title, room);
    connect(&services_.diagnosticLog(), &DiagnosticLog::recorded, d->log(), [log = d->log()](const LogRecord& r) {
        log->appendRaw(r.formatted());
    });
    d->flows()->setBottomPanels(panels_, d->propsTab(), d->capsTab(), d->updates());
    statusBar()->addPermanentWidget(d->indicator());
    registry_->track(d);
    wire(d);
}

// What of a document the window answers: menus on its canvas, questions about templates, its tab and the state shown.
void MainWindow::wire(Document* d) {
    Controller* c = d->controller();
    CanvasScene* scene = d->scene();
    auto isFront = [this, d] { return d == front(); };
    connect(scene, &CanvasScene::elementMenuRequested, d, [this, d](const QString& e, const QPoint& at) { menus_->element(*d, e, at); }, Qt::QueuedConnection);
    connect(scene, &CanvasScene::templateMenuRequested, d, [this, d](const QString& id, const QPoint& at) { menus_->templateFrame(*d, id, at); }, Qt::QueuedConnection);
    connect(scene, &CanvasScene::canvasContextMenuRequested, d, [this, d](const QPoint& at, const QPointF& scenePos) { menus_->canvas(*d, at, scenePos); }, Qt::QueuedConnection);
    connect(scene, &CanvasScene::linkMenuRequested, menus_, &ContextMenus::link, Qt::QueuedConnection);
    connect(scene, &QGraphicsScene::selectionChanged, this, [this, isFront] { if (isFront()) scheduleRefresh(); });
    connect(d->view(), &CanvasView::deletePressed, d, &Document::deleteSelection);
    connect(d->flows(), &Flows::componentMenuRequested, files_, &FileOperations::componentMenu);
    connect(d->flows(), &Flows::importRequested, files_, &FileOperations::importInto);
    // The document is their context: the caller's controller outlives the first tab when that tab closes.
    connect(c, &Controller::templateChanged, d, [this, isFront](const QString& id) { if (isFront()) askTemplateChange(id); }, Qt::QueuedConnection);
    connect(c, &Controller::templatesOutdated, d, [this, d, isFront](const QStringList& ids) {
        if (isFront()) askTemplatesOutdated(ids);
        else d->deferOutdated(ids);
    }, Qt::QueuedConnection);
    connect(c, &Controller::changed, d, [this, d] {
        tabs_->refresh(d);
        scheduleRefresh();
    });
    connect(c, &Controller::notification, d, [this](const Notice& n) { notify(n); });
}

void MainWindow::notify(const Notice& notice) {
    if (notifications_) notifications_->push(notice);
    statusBar()->showMessage(notice.summary(), 10000);
}

void MainWindow::appendDiagnostic(LogChannel channel, const QString& source, const QString& message, NoticeSeverity severity) {
    services_.diagnosticLog().append(channel, severity, source, message);
}

void MainWindow::activate(Document* d) { tabs_->activate(d); }

// Its pages in the panels, its indicator in the status bar; what was deferred while it was behind is asked now.
void MainWindow::activated(Document* d) {
    for (QWidget* page : d->panelPages()) panels_->bring(page);
    for (Document* other : documents()) other->indicator()->setActiveDocument(other == d);
    if (!built_) return;
    onEnvironment();
    const QStringList outdated = d->takeOutdated();
    if (!outdated.isEmpty()) QTimer::singleShot(0, d, [this, d, outdated] { if (d == front()) askTemplatesOutdated(outdated); });
    if (const auto why = d->takeChangedOnDisk()) QTimer::singleShot(0, d, [this, d, why] { if (d == front()) onChangedOnDisk(d, *why); });
}

Document* MainWindow::newDocument() { return tabs_->add(); }

bool MainWindow::closeDocument(Document* d) {
    if (!d || !documents().contains(d) || !saveFlow_->confirmClose({d})) return false;
    tabs_->close(d);
    scheduleRefresh();
    return true;
}

void MainWindow::restoreSession() { tabs_->restoreSession(); }

void MainWindow::openFile(const QString& path) {
    Settings::addRecent(path);
    tabs_->open(std::make_unique<ProjectConfTarget>(path));
}

// From a list in the drawer or the Explorer: the drawer's list has done its part.
Document* MainWindow::openComponent(const ComponentRef& component) {
    return openComponent(component, true);
}

Document* MainWindow::openTemplate(const QString& name) { return openComponent(TemplateLibrary::ref(name), false); }

Document* MainWindow::openComponent(const ComponentRef& component, bool dismissDrawer) {
    ComponentProvider* provider = services_.provider(component.provider);
    if (!provider) return nullptr;
    QString error;
    auto target = provider->documentTarget(component.id, ComponentProvider::DocumentAccess::Open, &error);
    if (dismissDrawer && (target || !error.isEmpty())) drawer()->dismiss();
    if (!target) {
        if (!error.isEmpty()) notify({NoticeSeverity::Error, "Unable to open component", error, false});
        return nullptr;
    }
    return tabs_->open(std::move(target));
}

void MainWindow::openCapsTab(const QString& srcElement, const QString& srcIdentity) { flows()->editCaps(srcElement, srcIdentity); }

// ---- the state shown

void MainWindow::scheduleRefresh() {
    if (std::exchange(refreshDue_, true)) return;
    QMetaObject::invokeMethod(this, [this] {
        refreshDue_ = false;
        refreshState();
    }, Qt::QueuedConnection);
}

void MainWindow::refreshState() {
    if (!built_) return;
    commands_->refresh();
    showExplorer();
    showStatus();
    setWindowTitle(BuildInfo::title() + " " + BuildInfo::version() + " – " + front()->title() + (controller()->hasUnsavedWork() ? " *" : ""));
}

// The front tab, its project and every tab's state; the Explorer's title counts the unsaved ones.
void MainWindow::showExplorer() {
    EditorState editor;
    editor.front = controller()->target().sessionKey();
    editor.projectConf = controller()->target().projectConf();
    for (const Document* d : documents()) {
        const bool unsaved = d->controller()->hasUnsavedWork();
        const QString key = d->controller()->target().sessionKey();
        editor.open[key] = d->fileDeleted() ? ExplorerEntry::State::Deleted
                                              : unsaved          ? ExplorerEntry::State::Unsaved
                                                                 : ExplorerEntry::State::Open;
        const UpdateResult& r = d->controller()->result();
        if (r.valid() && !r.ok()) editor.validation[key] = ExplorerEntry::Validation::Invalid;
        else if (r.valid() && r.ok() && r.complete()) editor.validation[key] = ExplorerEntry::Validation::Valid;
        else editor.validation[key] = ExplorerEntry::Validation::None;
        editor.unsaved += unsaved;
    }
    explorer_->present(editor);
    panels_->retitle(explorer_, explorer_->title());
}

void MainWindow::showStatus() {
    const Controller* c = controller();
    const UpdateResult& r = c->result();
    QString text;
    QString colour = Theme::text().name();
    if (const QString env = environmentReason(); !env.isEmpty()) text = env;
    else if (c->updating()) text = "Validating the latest changes… You can keep editing";
    else if (c->running()) text = "Running…";
    else if (c->monitoring()) text = "Monitoring live metrics…";
    else if (c->plan().empty()) text = "Empty canvas: choose Add source to start";
    else if (c->notValidated()) text = "Validation was cancelled — press Validate. Run is off", colour = Theme::warning().name();
    else if (r.valid() && !r.ok()) text = "Validation reported a problem. Run is off", colour = Theme::error().name();
    else if (r.valid() && !r.complete()) text = "Pipeline is not complete (free src pad). Run is off", colour = Theme::warning().name();
    else if (r.valid()) text = "Pipeline is complete", colour = Theme::accent().name();
    else text = "Not validated yet — press Validate";
    state_->setText(text);
    state_->setStyleSheet("color: " + colour + ";");
    const StartupResult& env = services_.environment().result();
    environment_->setText(env.state == StartupResult::Ready ? env.statusText() : QString());
    const QString command = c->launchCommand();
    if (command_->text() != command) {
        command_->setText(command);
        command_->setCursorPosition(0);
        command_->setToolTip(command);
    }
    command_->setVisible(!command.isEmpty());
}

// The front tab shows the setup page while GStreamer is not ready.
void MainWindow::onEnvironment() {
    const StartupResult& env = services_.environment().result();
    if (env.bad()) {
        setupPage()->show(env);
        front()->centre()->setCurrentWidget(setupPage());
        drawer()->dismiss();
        flows()->closeProperties();
        flows()->closeCaps();
    } else {
        front()->centre()->setCurrentIndex(0);
    }
    scheduleRefresh();
}

// ---- files changed by someone else

namespace {
// What the user reads about a tab whose file changed: another program wrote it, or another tab saved a newer revision
// of the template it edits to the library.
struct ChangeWords {
    QString title, headline, reloaded;
};
ChangeWords wordsFor(const Document* d, DiskChange why) {
    if (why == DiskChange::Library)
        return {"Template updated", d->title() + " has a newer revision, saved from another tab.", d->title() + " has a newer revision from another tab and was reloaded"};
    return {"File changed on disk", d->title() + " was changed on disk by another program.", d->title() + " changed on disk and was reloaded"};
}
}  // namespace

// Someone else changed a tab's file: without unsaved work it is read again; with some, the user chooses, when the tab
// is in front. Asked again when a deferred tab comes to the front, it may be clean or saved by then. A busy tab is
// asked once it has finished, once for all the changes meanwhile.
void MainWindow::onChangedOnDisk(Document* d, DiskChange why) {
    tabs_->refresh(d);                            // a deleted file that is back loses its mark
    scheduleRefresh();
    if (!registry_->changedSinceRead(d)) return;  // saved or reloaded meanwhile, or back as it was
    if (d->controller()->busy()) {
        whenIdle(d, [this, d, why] { onChangedOnDisk(d, why); });
    } else if (!d->controller()->hasUnsavedWork()) {
        d->controller()->reload();
        notify({NoticeSeverity::Info, "Reloaded from disk", wordsFor(d, why).reloaded, false});
    } else if (d == front()) {
        askReload(d, why);
    } else {
        d->deferChangedOnDisk(why);
    }
}

// The library wrote: a tab editing one of its templates whose file is not what it read was not the writer.
void MainWindow::onLibraryChanged() {
    for (Document* d : documents()) {
        if (!d->controller()->target().libraryTemplate().isEmpty() && registry_->catchUp(d)) onChangedOnDisk(d, DiskChange::Library);
    }
}

// Opened, not run: the event loop goes on (saves, runs, the watcher) while it waits.
void MainWindow::askReload(Document* d, DiskChange why) {
    const ChangeWords words = wordsFor(d, why);
    QPointer<Document> asked(d);
    Prompt::askLater(this, {"changedOnDisk", words.title, words.headline, "Reload it and lose your changes, or keep your version?",
                            {{"Reload", "reloadFromDisk", QMessageBox::DestructiveRole}, {"Keep mine", "keepMine", QMessageBox::RejectRole}},
                            1, QMessageBox::Warning},
                     [this, asked](int answer) {
        if (!asked) return;
        if (answer == 0) whenIdle(asked, [asked] { if (asked) asked->controller()->reload(); });  // a validation may still run
        else registry_->accept(asked);
    });
}

void MainWindow::whenIdle(Document* d, const std::function<void()>& then) {
    if (!d->controller()->busy()) {
        then();
    } else if (d->startWaiting()) {
        QTimer::singleShot(DocumentRegistry::kSettleMs, d, [this, d, then] {
            d->stopWaiting();
            whenIdle(d, then);
        });
    }
}

// ---- templates

// "Template changes, what now?" (TEMPLATE_PLAN D17, D30): the change, the projects the template is used in, and four
// answers. Closing the window is Undo: nothing was decided.
void MainWindow::askTemplateChange(const QString& id) {
    const QString name = controller()->templateOf(id);
    if (name.isEmpty()) return;
    QStringList used;
    for (const QString& project : services_.library().projectsUsing(name)) used << QFileInfo(project).absolutePath();
    const QString details = "Change: " + controller()->lastAction() + "\n\n" +
                            (used.isEmpty() ? QString("No saved project uses this template yet.")
                                            : "Used in:\n  " + used.join("\n  ") + "\n\nUpdating it changes these projects too.");
    enum { Update, AsNew, Detach };
    const int answer = Prompt::ask(this, {"templateChange", "Template changes", QString("<b>'%1' template changes</b>").arg(name.toHtmlEscaped()), details,
                                          {{"Update template", "updateTemplate"}, {"Save as new template…", "saveAsNewTemplate", QMessageBox::ActionRole},
                                           {"Detach", "detachTemplate", QMessageBox::ActionRole}, {"Undo", "undoTemplateChange", QMessageBox::RejectRole}}});
    if (answer == Update) {
        controller()->keepEditingTemplate(id);
        statusBar()->showMessage("Editing template " + name + ": the change goes to the library when it collapses or the file is saved.", 8000);
    } else if (answer == AsNew) {
        const auto newName = Prompt::text(this, "Save as new template", "Name of the new template:", name + " 2");
        QString error;
        if (!newName || !controller()->saveTemplateAsNew(id, *newName, &error)) {
            if (!error.isEmpty()) Prompt::error(this, "Save as new template", error);
            controller()->undo();
        }
    } else if (answer == Detach) {
        controller()->detachTemplate(id);
    } else {
        controller()->undo();
    }
}

// An opened file holds older versions of templates the library has newer ones of.
void MainWindow::askTemplatesOutdated(const QStringList& ids) {
    QStringList lines;
    for (const QString& id : ids) {
        const gps::TemplateInstance* t = controller()->plan().instance(toStd(id));
        if (!t) continue;
        const QString name = fromStd(t->templateName);
        lines << QString("%1: revision %2 → %3%4").arg(name).arg(t->revision).arg(services_.library().revision(name))
                     .arg(gps::templates::modified(controller()->plan(), t->id) ? "  (its changes here would be lost)" : "");
    }
    if (lines.isEmpty()) return;
    if (Prompt::yes(this, "Templates were updated",
                    "The library has newer versions of templates this file uses:\n\n  " + lines.join("\n  ") + "\n\nUse the newer versions?"))
        controller()->updateTemplates(ids);
}

// ---- dialogs

// The earlier versions of the open file, newest first; the chosen one comes back as an unsaved change.
void MainWindow::showVersionHistory() {
    const QString file = front()->path();
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
    if (dialog.exec() != QDialog::Accepted || !list->currentItem()) return;  // one undoable change: nothing to ask
    controller()->restoreVersion(list->currentItem()->data(Qt::UserRole).toString());
}

std::optional<QString> MainWindow::askLaunchDescription(const QString& title) {
    QString initial;
    if (const QClipboard* clipboard = QGuiApplication::clipboard()) {
        const QString text = clipboard->text();
        if (gps::looksLikeLaunchText(text.toStdString())) initial = text.trimmed();
    }
    const auto description = Prompt::lines(this, title,
        "Paste a gst-launch-1.0 command or its pipeline description.\n"
        "The pipeline runs briefly in the background only if dynamic pads must be discovered.",
        initial);
    if (!description || description->trimmed().isEmpty()) return std::nullopt;
    return description;
}

void MainWindow::importLaunch() {
    const auto description = askLaunchDescription("New from gst-launch");
    if (!description) return;
    Document* d = tabs_->target();
    d->opening("imported");
    d->controller()->importLaunch(*description);
}

void MainWindow::exportSharedLibrary() {
    if (controller()->plan().empty() || controller()->busy()) return;
    const QString outDir = QFileDialog::getExistingDirectory(this, "Export Shared Library", Settings::lastDir());
    if (outDir.isEmpty()) return;
    const QString defaultName = PipelineCompiler::sanitizeModuleName(front()->title());
    const auto moduleName = Prompt::text(this, "Export Shared Library",
                                         "Module name (lowercase letters, digits, underscores):", defaultName);
    if (!moduleName) return;
    if (const QString nameErr = PipelineCompiler::validateModuleName(*moduleName); !nameErr.isEmpty()) {
        Prompt::error(this, "Export Shared Library", nameErr);
        return;
    }

    PipelineExportOptions opts;
    opts.moduleName = *moduleName;
    opts.outputDir = outDir;
    const gps::Graph plan = controller()->plan();
    services_.diagnosticLog().info(LogChannel::PluginBuild, opts.moduleName,
                                   "Exporting shared library to " + opts.outputDir + "…");
    notify({NoticeSeverity::Info, "Exporting shared library", "Building lib" + opts.moduleName + ".so…", false});

    QPointer<MainWindow> self(this);
    PipelineCompiler::compileAsync(
        plan, opts, this,
        [self, name = opts.moduleName](const QString& line) {
            if (self) self->services_.diagnosticLog().info(LogChannel::PluginBuild, name, line);
        },
        [self, name = opts.moduleName](const PipelineExportResult& result) {
            if (!self) return;
            if (result.ok) {
                self->services_.diagnosticLog().info(LogChannel::PluginBuild, name, "Exported " + result.soPath);
                self->notify({NoticeSeverity::Info, "Shared library exported", result.soPath, false});
            } else {
                self->services_.diagnosticLog().error(LogChannel::PluginBuild, name, result.error);
                self->notify({NoticeSeverity::Error, "Shared library export failed", result.error, false});
            }
        },
        &services_.plugins());
}

void MainWindow::addLaunch() {
    const QPointF at = commands_->target().scenePos;
    commands_->setTarget({});
    const auto description = askLaunchDescription("Add from gst-launch");
    if (!description) return;
    Document* d = front();
    d->controller()->addLaunch(*description, at.x(), at.y(), [doc = QPointer<Document>(d)](const QStringList& created) {
        if (doc && !created.isEmpty()) doc->scene()->setSelectedElements(created);
    });
}

// The drawer's search when it is open, else the Properties tab's first field.
void MainWindow::focusSearch() {
    if (drawer()->isOpen()) drawer()->focusSearch();
    else if (flows()->propertyPage()) flows()->propertyPage()->focusFirst();
}

void MainWindow::showWelcomeTour() {
    OnboardingDialog dialog(this);
    dialog.exec();
}

void MainWindow::showAbout() {
    AboutDialog dialog(services_.environment().result().statusText(), this);
    dialog.exec();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!files_->closePluginEditors()) {
        event->ignore();
        return;
    }
    for (Document* d : documents()) {
        d->controller()->stopAll();
        d->backup()->flush();
    }
    tabs_->saveSession();
    Settings::setWindow(saveGeometry(), saveState());
    event->accept();
}
