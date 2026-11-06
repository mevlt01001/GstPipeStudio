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
#include "capspage.h"
#include "controller.h"
#include "drawer.h"
#include "elementitem.h"
#include "flows.h"
#include "linkitem.h"
#include "logpanel.h"
#include "propertypage.h"
#include "setuppage.h"
#include "theme.h"
#include "updatepanel.h"
#include "validationindicator.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QInputDialog>
#include <QVBoxLayout>
#include <set>

MainWindow::MainWindow(Controller* controller, QWidget* parent) : QMainWindow(parent), controller_(controller) {
    setObjectName("mainWindow");
    setMinimumSize(960, 600);
    resize(1360, 860);

    drawer_ = new Drawer(this);
    flows_ = new Flows(controller, drawer_, this);
    scene_ = new CanvasScene(controller, this);
    view_ = new CanvasView(scene_, this);

    // The drawer slides in on the right side of the canvas; the canvas keeps its own scroll position.
    auto* canvasPage = new QWidget(this);
    auto* canvasRow = new QHBoxLayout(canvasPage);
    canvasRow->setContentsMargins(0, 0, 0, 0);
    canvasRow->setSpacing(0);
    canvasRow->addWidget(view_, 1);
    canvasRow->addWidget(drawer_);
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
    connect(scene_, &CanvasScene::linkClickedSignal, flows_, &Flows::editCaps, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::linkMenuRequested, flows_, &Flows::showLinkMenu, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::elementMenuRequested, this, &MainWindow::onElementMenuRequested, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::canvasContextMenuRequested, this, [this](const QPoint& screenPos, const QPointF&) { showCanvasMenu(screenPos); }, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::elementActivated, flows_, &Flows::editProperties, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::elementsMoved, controller_, &Controller::setPositions, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::elementSpliced, this, [this](const QString& element, const QString& linkSrc, const QString& linkPad) {
        QStringList sel = scene_->selectedElementNames();
        if (sel.contains(element) && sel.size() > 1) {
            controller_->spliceElements(sel, linkSrc, linkPad);
        } else {
            controller_->spliceElement(element, linkSrc, linkPad);
        }
    }, Qt::QueuedConnection);
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
    auto* addElement = make("actionAddElement", "Add &element…", {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E)}, [this] { flows_->chooseElement(); });
    addElement->setIconText("Add element");
    auto* undoAct = make("actionUndo", "&Undo", {QKeySequence::Undo}, [this] { controller_->undo(); });
    undoAct->setIconText("Undo");
    auto* redoAct = make("actionRedo", "&Redo", {QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)}, [this] { controller_->redo(); });
    redoAct->setIconText("Redo");
    auto* dupAct = make("actionDuplicate", "&Duplicate", {QKeySequence(Qt::CTRL | Qt::Key_D)}, [this] { duplicateSelected(); });
    auto* selAllAct = make("actionSelectAll", "Select &All", {QKeySequence::SelectAll}, [this] { selectAll(); });
    auto* delAct = make("actionDelete", "De&lete", {QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)}, [this] { deleteSelected(); });
    auto* copyAct = make("actionCopy", "&Copy", {QKeySequence::Copy}, [this] { copySelected(); });
    auto* cutAct = make("actionCut", "Cu&t", {QKeySequence::Cut}, [this] { cutSelected(); });
    auto* pasteAct = make("actionPaste", "&Paste", {QKeySequence::Paste}, [this] { pasteClipboard(); });
    auto* connectSelAct = make("actionConnectSelected", "Connect &selected", {QKeySequence(Qt::CTRL | Qt::Key_L)}, [this] { connectSelected(); });
    auto* saveCustomAct = make("actionSaveCustomElement", "Save as &custom element…", {QKeySequence(Qt::CTRL | Qt::Key_G)}, [this] { saveSelectedAsCustomElement(); });
    saveCustomAct->setIconText("Save custom");
    auto* find = make("actionFind", "&Find element", {QKeySequence::Find}, [this] {
        if (drawer_->isOpen()) drawer_->focusSearch();
        else if (flows_->propertyPage()) flows_->propertyPage()->focusFirst();
    });
    auto* showLog = make("actionShowLog", "&Log", {}, [this] { tabs_->setCurrentWidget(log_); });
    auto* showValidation = make("actionShowValidation", "&Validation", {}, [this] { tabs_->setCurrentWidget(updates_); });
    auto* showProps = make("actionShowProperties", "&Properties", {}, [this] { tabs_->setCurrentWidget(propsTab_); });
    auto* showCaps = make("actionShowCaps", "&Caps", {}, [this] { tabs_->setCurrentWidget(capsTab_); });
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
    pipeline->addActions({addSource, addElement, update, run});
    pipeline->addSeparator();
    pipeline->addActions({undoAct, redoAct});
    pipeline->addSeparator();
    pipeline->addActions({copyAct, cutAct, pasteAct, dupAct, selAllAct, delAct});
    pipeline->addSeparator();
    pipeline->addActions({connectSelAct, saveCustomAct});
    QMenu* viewMenu = menuBar()->addMenu("&View");
    viewMenu->addActions({showLog, showValidation, showProps, showCaps, find});
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
    bar->addActions({undoAct, redoAct});
    bar->addSeparator();
    bar->addActions({update, run});
    bar->addSeparator();
    bar->addAction(addSource);
    bar->addAction(addElement);
    bar->addAction(saveCustomAct);
    bar->widgetForAction(run)->setObjectName("primaryAction");
}

void MainWindow::buildPanels() {
    log_ = new LogPanel(this);
    updates_ = new UpdatePanel(controller_, this);
    propsTab_ = new QWidget(this);
    propsTab_->setObjectName("propsTab");
    new QVBoxLayout(propsTab_);
    capsTab_ = new QWidget(this);
    capsTab_->setObjectName("capsTab");
    new QVBoxLayout(capsTab_);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName("bottomTabs");
    tabs_->addTab(log_, "Log");
    tabs_->addTab(updates_, "Validation");
    tabs_->addTab(propsTab_, "Properties");
    tabs_->addTab(capsTab_, "Caps");
    tabs_->setCurrentWidget(updates_);
    flows_->setBottomPanels(tabs_, propsTab_, capsTab_, updates_);
    auto* bottom = new QDockWidget("Panels", this);
    bottom->setObjectName("bottomDock");
    bottom->setFeatures(QDockWidget::NoDockWidgetFeatures);
    bottom->setTitleBarWidget(new QWidget(bottom));  // the tabs are the title
    bottom->setWidget(tabs_);
    bottom->setMinimumHeight(180);
    addDockWidget(Qt::BottomDockWidgetArea, bottom);
    resizeDocks({bottom}, {240}, Qt::Vertical);
    // Properties and Caps are forms: the panel grows so their fields stay usable (never shrinks what the user chose).
    connect(tabs_, &QTabWidget::currentChanged, this, [this, bottom] {
        QWidget* cur = tabs_->currentWidget();
        if ((cur == propsTab_ || cur == capsTab_) && bottom->height() < 400) resizeDocks({bottom}, {400}, Qt::Vertical);
    });

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
    bool editBlocked = controller_->editingBlocked();
    bool hasPlan = !controller_->plan().empty();

    act("actionNew")->setEnabled(!busy && !blocked);
    act("actionOpen")->setEnabled(!busy && !blocked);
    act("actionAddSource")->setEnabled(!editBlocked && !blocked);
    act("actionAddElement")->setEnabled(!editBlocked && !blocked);
    act("actionUndo")->setEnabled(controller_->canUndo());
    act("actionRedo")->setEnabled(controller_->canRedo());
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
    act("actionAddSource")->setToolTip(blocked ? "Add source is off. GStreamer is not ready" : editBlocked ? "Add source is off. Wait for the running step to finish" : "Add a source element (Ctrl+E)");

    const UpdateResult& r = controller_->result();
    QString text;
    QString colour = Theme::text().name();
    if (blocked) text = "GStreamer is not ready";
    else if (controller_->updating()) text = "Validating the latest changes… You can keep editing";
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

    scene_->setLocked(editBlocked);
    scene_->setStale(controller_->notValidated());
    view_->setDimmed(controller_->running());
    drawer_->setBusy(editBlocked);
    flows_->setBusy(editBlocked);

    QString name = controller_->filePath().isEmpty() ? "untitled" : QFileInfo(controller_->filePath()).fileName();
    setWindowTitle("GstPipeStudio – " + name + (controller_->dirty() ? " *" : ""));
}

void MainWindow::onEnvironment() {
    const StartupResult& env = controller_->environment();
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

void MainWindow::onRebuilt() {
    QStringList names;
    for (const gps::Element& e : controller_->plan().elements) names << QString::fromStdString(e.name);
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

void MainWindow::showCanvasMenu(const QPoint& screenPos) {
    if (controller_->editingBlocked() || controller_->environmentBlocked()) return;
    auto* menu = new QMenu(this);
    menu->setObjectName("canvasMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction("Add element…", this, [this] { flows_->chooseElement(); })->setObjectName("actionCanvasAddElement");
    menu->addAction("Add source…", this, [this] { flows_->chooseSource(); })->setObjectName("actionCanvasAddSource");
    if (!controller_->customElements().isEmpty()) {
        QMenu* customSub = menu->addMenu("Add custom element");
        for (const CustomElementInfo& c : controller_->customElements()) {
            customSub->addAction(c.name + "  (" + c.description + ")", this, [this, name = c.name] {
                QStringList created = controller_->addCustomElement(name);
                if (!created.isEmpty()) scene_->setSelectedElements(created);
            });
        }
    }
    QStringList sel = scene_->selectedElementNames();
    if (!sel.isEmpty()) {
        menu->addSeparator();
        menu->addAction(QString("Save selection (%1) as custom element…").arg(sel.size()), this, [this] { saveSelectedAsCustomElement(); })
            ->setObjectName("actionCanvasSaveCustom");
        if (sel.size() >= 2) {
            menu->addAction("Connect selected elements", this, [this] { connectSelected(); })->setObjectName("actionCanvasConnectSelected");
            menu->addAction("Align horizontally", this, [this, sel] { controller_->alignElementsHorizontally(sel); });
            menu->addAction("Align vertically", this, [this, sel] { controller_->alignElementsVertically(sel); });
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
    auto* v = menu->addAction("Validate", this, [this] { controller_->refresh(); });
    v->setEnabled(!controller_->plan().empty());
    menu->popup(screenPos);
}

void MainWindow::onElementMenuRequested(const QString& element, const QPoint& screenPos) {
    if (controller_->editingBlocked()) return;
    QStringList sel = scene_->selectedElementNames();
    QString grp = controller_->elementGroup(element);
    if (sel.size() > 1 || !grp.isEmpty()) {
        showSelectionMenu(element, screenPos);
    } else {
        flows_->showElementMenu(element, screenPos);
    }
}

void MainWindow::showSelectionMenu(const QString& clickedElement, const QPoint& screenPos) {
    if (controller_->editingBlocked()) return;
    QStringList sel = scene_->selectedElementNames();
    if (!sel.contains(clickedElement)) sel << clickedElement;
    sel = controller_->expandSelectionWithGroups(sel);

    auto* menu = new QMenu(this);
    menu->setObjectName("selectionMenu");
    menu->setAttribute(Qt::WA_DeleteOnClose);

    QString grp = controller_->elementGroup(clickedElement);
    if (!grp.isEmpty()) {
        QString title = grp.section('#', 0, 0);
        bool expanded = controller_->isGroupExpanded(grp);
        menu->addAction(expanded ? ("Collapse '" + title + "' to single element") : ("Expand '" + title + "' (" + QString::number(controller_->groupMembers(grp).size()) + " elements)"),
                        this, [this, grp, expanded] { controller_->setGroupExpanded(grp, !expanded); })
            ->setObjectName("actionToggleGroupExpand");
        menu->addAction("Ungroup '" + title + "'", this, [this, grp] { controller_->ungroupElements(grp); })->setObjectName("actionUngroup");
        if (expanded) {
            menu->addAction("Edit properties of " + clickedElement + "…", this, [this, clickedElement] { flows_->editProperties(clickedElement); });
        }
        menu->addSeparator();
    }

    menu->addAction(QString("Save as custom element (%1)…").arg(sel.size()), this, [this] { saveSelectedAsCustomElement(); })
        ->setObjectName("actionSaveCustomElementMenu");
    if (grp.isEmpty() && sel.size() >= 2) {
        menu->addAction("Combine into single group on canvas", this, [this, sel] {
            controller_->groupSelectedElements(sel, "CustomChain");
        })->setObjectName("actionGroupSelected");
    }
    if (sel.size() >= 2) {
        menu->addSeparator();
        menu->addAction("Connect selected elements", this, [this, sel] { controller_->connectSelectedElements(sel); })
            ->setObjectName("actionConnectSelectedMenu");
        menu->addAction("Disconnect links between selected", this, [this, sel] { controller_->disconnectBetweenElements(sel); })
            ->setObjectName("actionDisconnectSelectedMenu");
        menu->addAction("Align horizontally", this, [this, sel] { controller_->alignElementsHorizontally(sel); })
            ->setObjectName("actionAlignHorizontally");
        menu->addAction("Align vertically", this, [this, sel] { controller_->alignElementsVertically(sel); })
            ->setObjectName("actionAlignVertically");
    }
    menu->addSeparator();
    menu->addAction(QString("Duplicate (%1)").arg(sel.size()), this, [this] { duplicateSelected(); })->setObjectName("actionDuplicateSelection");
    menu->addAction(QString("Copy (%1)").arg(sel.size()), this, [this] { copySelected(); })->setObjectName("actionCopySelection");
    menu->addAction(QString("Cut (%1)").arg(sel.size()), this, [this] { cutSelected(); })->setObjectName("actionCutSelection");
    menu->addAction(QString("Delete (%1)").arg(sel.size()), this, [this] { deleteSelected(); })->setObjectName("actionDeleteSelection");
    menu->popup(screenPos);
}

void MainWindow::saveSelectedAsCustomElement() {
    if (controller_->editingBlocked()) return;
    QStringList sel = scene_->selectedElementNames();
    if (sel.isEmpty()) {
        statusBar()->showMessage("Select one or more elements on the canvas first to save as a custom element.", 6000);
        return;
    }
    QStringList factories;
    for (const QString& n : sel) {
        if (const gps::Element* e = controller_->plan().find(n.toStdString())) {
            factories << QString::fromStdString(e->factory);
        }
    }
    QString suggested = factories.join("_");
    if (suggested.size() > 28) suggested = suggested.left(28);
    bool ok = false;
    QString name = QInputDialog::getText(this, "Save as Custom Element",
                                         QString("Save %1 selected element(s) as a reusable Custom Element:\n(%2)").arg(sel.size()).arg(factories.join(" → ")),
                                         QLineEdit::Normal, suggested, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    if (controller_->saveCustomElement(name.trimmed(), sel)) {
        statusBar()->showMessage(QString("Saved custom element '%1' (%2 elements). Available in Add Element.").arg(name.trimmed()).arg(sel.size()), 8000);
    }
}

void MainWindow::connectSelected() {
    QStringList sel = scene_->selectedElementNames();
    if (sel.size() >= 2) controller_->connectSelectedElements(sel);
}

// Delete on the canvas: a single selected source asks confirmation; multi-selection or non-source elements/links delete atomically.
void MainWindow::deleteSelected() {
    QStringList selectedElements = scene_->selectedElementNames();
    QList<QPair<QString, QString>> selectedLinks;
    for (QGraphicsItem* item : scene_->selectedItems()) {
        if (auto* link = dynamic_cast<LinkItem*>(item)) {
            selectedLinks.append({link->view().srcElement, link->view().srcIdentity});
        }
    }
    if (selectedElements.size() == 1 && selectedLinks.isEmpty()) {
        const QString& only = selectedElements.first();
        if (controller_->elementGroup(only).isEmpty() && controller_->view(only).isSource) {
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
