// mainwindow.cpp
#include "mainwindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>

#include "canvasscene.h"
#include "canvasview.h"
#include "controller.h"
#include "drawer.h"
#include "elementitem.h"
#include "flows.h"
#include "logpanel.h"
#include "setuppage.h"
#include "theme.h"
#include "updatepanel.h"
#include "validationindicator.h"

MainWindow::MainWindow(Controller* controller, QWidget* parent) : QMainWindow(parent), controller_(controller) {
    setObjectName("mainWindow");
    setMinimumSize(960, 600);
    resize(1360, 860);

    drawer_ = new Drawer(this);
    flows_ = new Flows(controller, drawer_, this);
    scene_ = new CanvasScene(controller, this);
    view_ = new CanvasView(scene_, this);

    // The drawer slides in beside the canvas; the canvas keeps its own scroll position.
    auto* canvasPage = new QWidget(this);
    auto* canvasRow = new QHBoxLayout(canvasPage);
    canvasRow->setContentsMargins(0, 0, 0, 0);
    canvasRow->setSpacing(0);
    canvasRow->addWidget(drawer_);
    canvasRow->addWidget(view_, 1);
    setup_ = new SetupPage(this);
    centre_ = new QStackedWidget(this);
    centre_->setObjectName("centre");
    centre_->addWidget(canvasPage);
    centre_->addWidget(setup_);
    setCentralWidget(centre_);

    buildActions();
    buildPanels();

    // The canvas reports clicks; the work happens after the item's event handler has returned.
    connect(scene_, &CanvasScene::freeSrcPadClicked, flows_, [this](const QString& e, const QString& id, const QPoint&) { flows_->fromFreePad(e, id); }, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::linkMenuRequested, flows_, &Flows::showLinkMenu, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::elementMenuRequested, flows_, &Flows::showElementMenu, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::elementActivated, flows_, &Flows::editProperties, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::elementMoved, controller_, &Controller::setPosition, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::rebuilt, this, &MainWindow::onRebuilt);
    connect(view_, &CanvasView::deletePressed, this, &MainWindow::deleteSelected);
    connect(setup_, &SetupPage::retry, controller_, &Controller::checkEnvironment);

    connect(controller_, &Controller::changed, scene_, &CanvasScene::scheduleRebuild);
    connect(controller_, &Controller::changed, this, &MainWindow::refreshState);
    connect(controller_, &Controller::environmentChecked, this, &MainWindow::onEnvironment);
    connect(controller_, &Controller::logLine, log_, &LogPanel::append);
    connect(controller_, &Controller::notice, this, [this](const QString& text) {
        log_->append(text);
        statusBar()->showMessage(text, 10000);
    });

    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    restoreState(settings.value("window/state").toByteArray());
    refreshState();
    controller_->checkEnvironment();
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
        if (confirmDiscard()) controller_->newPlan();
    });
    auto* open = make("actionOpen", "&Open…", {QKeySequence::Open}, [this] {
        if (!confirmDiscard()) return;
        QSettings settings;
        QString path = QFileDialog::getOpenFileName(this, "Open pipeline", settings.value("lastDir").toString(), "Pipeline (*.conf)");
        if (!path.isEmpty()) openFile(path);
    });
    auto* save = make("actionSave", "&Save", {QKeySequence::Save}, [this] {
        if (controller_->filePath().isEmpty()) saveAs();
        else saveFile(QString());
    });
    auto* saveAs = make("actionSaveAs", "Save &as…", {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)}, [this] { this->saveAs(); });
    auto* update = make("actionUpdate", "&Validate", {QKeySequence(Qt::CTRL | Qt::Key_R), QKeySequence(Qt::Key_F5)}, [this] { controller_->refresh(); });
    auto* run = make("actionRun", "&Run", {QKeySequence(Qt::CTRL | Qt::Key_Return)}, [this] { toggleRun(); });
    auto* addSource = make("actionAddSource", "Add &source…", {QKeySequence(Qt::CTRL | Qt::Key_E)}, [this] { flows_->chooseSource(); });
    addSource->setIconText("Add source");
    auto* find = make("actionFind", "&Find element", {QKeySequence::Find}, [this] { drawer_->focusSearch(); });
    auto* showLog = make("actionShowLog", "&Log", {}, [this] { tabs_->setCurrentWidget(log_); });
    auto* showValidation = make("actionShowValidation", "&Validation", {}, [this] { tabs_->setCurrentWidget(updates_); });
    auto* fit = make("actionFit", "Zoom to &fit", {QKeySequence(Qt::CTRL | Qt::Key_0)}, [this] { view_->fitAll(); });
    auto* zoomIn = make("actionZoomIn", "Zoom &in", {QKeySequence::ZoomIn}, [this] { view_->zoomBy(1.25); });
    auto* zoomOut = make("actionZoomOut", "Zoom &out", {QKeySequence::ZoomOut}, [this] { view_->zoomBy(0.8); });
    auto* quit = make("actionQuit", "&Quit", {QKeySequence::Quit}, [this] { close(); });
    auto* support = make("actionSupport", "&Request support…", {}, [this] { controller_->requestSupport("Problem report"); });
    auto* about = make("actionAbout", "&About GstPipeStudio", {}, [this] { showAbout(); });
    auto* gstInfo = make("actionGstInfo", "&GStreamer information", {}, [this] {
        const StartupResult& env = controller_->environment();
        QMessageBox::information(this, "GStreamer information", env.version.isEmpty() ? "GStreamer was not checked or not found." : env.statusText());
    });

    QMenu* file = menuBar()->addMenu("&File");
    file->addActions({newPlan, open, save, saveAs});
    file->addSeparator();
    file->addAction(quit);
    QMenu* pipeline = menuBar()->addMenu("&Pipeline");
    pipeline->addActions({addSource, update, run});
    QMenu* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addActions({showLog, showValidation, find});
    viewMenu->addSeparator();
    viewMenu->addActions({fit, zoomIn, zoomOut});
    QMenu* help = menuBar()->addMenu("&Help");
    help->addActions({support, gstInfo, about});

    QToolBar* bar = addToolBar("Tools");
    bar->setObjectName("toolbar");
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bar->addActions({newPlan, open, save, saveAs});
    bar->addSeparator();
    bar->addActions({update, run});
    bar->addSeparator();
    bar->addAction(addSource);
    bar->widgetForAction(run)->setObjectName("primaryAction");
}

void MainWindow::buildPanels() {
    log_ = new LogPanel(this);
    updates_ = new UpdatePanel(controller_, this);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName("bottomTabs");
    tabs_->addTab(log_, "Log");
    tabs_->addTab(updates_, "Validation");
    tabs_->setCurrentWidget(updates_);
    auto* bottom = new QDockWidget("Panels", this);
    bottom->setObjectName("bottomDock");
    bottom->setFeatures(QDockWidget::NoDockWidgetFeatures);
    bottom->setTitleBarWidget(new QWidget(bottom));  // the tabs are the title
    bottom->setWidget(tabs_);
    bottom->setMinimumHeight(170);
    addDockWidget(Qt::BottomDockWidgetArea, bottom);
    resizeDocks({bottom}, {200}, Qt::Vertical);

    state_ = new QLabel(this);
    state_->setObjectName("stateLabel");
    environment_ = new QLabel(this);
    environment_->setObjectName("environmentLabel");
    environment_->setStyleSheet("color: " + Theme::muted().name() + ";");
    command_ = new QLabel(this);
    command_->setObjectName("commandLabel");
    command_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    command_->setStyleSheet("font-family: 'DejaVu Sans Mono'; color: " + Theme::muted().name() + ";");
    indicator_ = new ValidationIndicator(controller_, this);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(state_);
    statusBar()->addWidget(environment_);
    statusBar()->addPermanentWidget(command_);
    statusBar()->addPermanentWidget(indicator_);
}

// A disabled action says why, in its tooltip.
void MainWindow::setReason(QAction* action, const QString& text, const QString& reason) {
    action->setToolTip(reason.isEmpty() ? text : text + " is off. " + reason);
}

void MainWindow::refreshState() {
    auto act = [this](const char* name) { return findChild<QAction*>(name); };
    bool blocked = controller_->environmentBlocked();
    bool busy = controller_->busy();
    bool hasPlan = !controller_->plan().empty();

    act("actionNew")->setEnabled(!busy && !blocked);
    act("actionOpen")->setEnabled(!busy && !blocked);
    act("actionAddSource")->setEnabled(!busy && !blocked);
    act("actionSave")->setEnabled(controller_->canSave());
    act("actionSaveAs")->setEnabled(controller_->canSave());
    act("actionUpdate")->setEnabled(!busy && hasPlan);
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
    act("actionAddSource")->setToolTip(blocked ? "Add source is off. GStreamer is not ready" : busy ? "Add source is off. Wait for the running step to finish" : "Add a source element (Ctrl+E)");

    const UpdateResult& r = controller_->result();
    QString text;
    QString colour = Theme::text().name();
    if (blocked) text = "GStreamer is not ready";
    else if (controller_->updating()) text = "The canvas is locked while validating";
    else if (controller_->running()) text = "Running…";
    else if (!hasPlan) text = "Empty canvas: choose Add source to start";
    else if (controller_->notValidated()) text = "Validation was cancelled — press Validate. Save and Run are off", colour = Theme::warning().name();
    else if (r.valid() && !r.ok()) text = "Validation reported a problem. Save and Run are off", colour = Theme::error().name();
    else if (r.valid() && !r.complete) text = "Pipeline is not complete (free src pad). Save is on, Run is off", colour = Theme::warning().name();
    else if (r.valid()) text = "Pipeline is complete", colour = Theme::accent().name();
    else text = "Not validated yet — press Validate";
    state_->setText(text);
    state_->setStyleSheet("color: " + colour + ";");
    const StartupResult& env = controller_->environment();
    environment_->setText(env.state == StartupResult::Ready ? env.statusText() : QString());
    command_->setText(controller_->launchCommand());

    scene_->setLocked(controller_->updating());
    scene_->setStale(controller_->notValidated());
    view_->setDimmed(controller_->updating());
    drawer_->setBusy(busy);

    QString name = controller_->filePath().isEmpty() ? "untitled" : QFileInfo(controller_->filePath()).fileName();
    setWindowTitle("GstPipeStudio – " + name + (controller_->dirty() ? " *" : ""));
}

void MainWindow::onEnvironment() {
    const StartupResult& env = controller_->environment();
    if (env.bad()) {
        setup_->show(env);
        centre_->setCurrentWidget(setup_);
        drawer_->dismiss();
    } else {
        centre_->setCurrentIndex(0);
    }
    refreshState();
}

void MainWindow::onRebuilt() {
    QStringList names;
    for (const gps::PlanElement& e : controller_->plan().elements) names << QString::fromStdString(e.name);
    bool changed = names != lastNames_;
    lastNames_ = names;
    if (changed && !names.isEmpty() && !fitNext_) QTimer::singleShot(0, view_, &CanvasView::fitAll);
    if (fitNext_ && !controller_->busy() && !names.isEmpty()) {
        fitNext_ = false;
        QTimer::singleShot(0, view_, &CanvasView::fitAll);
    }
}

void MainWindow::openFile(const QString& path) {
    fitNext_ = true;
    QSettings().setValue("lastDir", QFileInfo(path).absolutePath());
    controller_->open(path);
}

bool MainWindow::saveFile(const QString& path) {
    QString error;
    if (controller_->save(path, &error)) {
        if (!path.isEmpty()) QSettings().setValue("lastDir", QFileInfo(path).absolutePath());
        return true;
    }
    statusBar()->showMessage(error, 10000);
    log_->append(error);
    return false;
}

void MainWindow::saveAs() {
    QSettings settings;
    QString start = controller_->filePath().isEmpty() ? settings.value("lastDir").toString() : controller_->filePath();
    QString path = QFileDialog::getSaveFileName(this, "Save pipeline as", start, "Pipeline (*.conf)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".conf")) path += ".conf";
    saveFile(path);
}

void MainWindow::toggleRun() {
    if (controller_->running()) controller_->stopRun();
    else controller_->run();
}

// Delete on the canvas: the selected source goes, after the usual confirmation.
void MainWindow::deleteSelected() {
    for (QGraphicsItem* item : scene_->selectedItems()) {
        auto* element = dynamic_cast<ElementItem*>(item);
        if (element && element->view().isSource) {
            flows_->deleteSource(element->view().name);
            return;
        }
    }
}

void MainWindow::showAbout() {
    QMessageBox::about(this, "About GstPipeStudio",
                       "GstPipeStudio builds GStreamer pipelines on a canvas.\n\nIt validates every step with the real GStreamer and saves a "
                       "file that gst-launch-1.0 runs through the gpsconf plugin.");
}

bool MainWindow::confirmDiscard() {
    if (!controller_->dirty() || controller_->plan().empty()) return true;
    auto answer = QMessageBox::question(this, "Unsaved changes", "The changes on the canvas have not been saved. Discard them?",
                                        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    return answer == QMessageBox::Discard;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (controller_->running()) controller_->stopRun();
    if (!confirmDiscard()) {
        event->ignore();
        return;
    }
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", saveState());
    event->accept();
}
