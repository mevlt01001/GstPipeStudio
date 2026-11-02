// mainwindow.cpp
#include "mainwindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>

#include "canvasscene.h"
#include "canvasview.h"
#include "controller.h"
#include "flows.h"
#include "logpanel.h"
#include "sourcepanel.h"
#include "theme.h"
#include "updatepanel.h"

MainWindow::MainWindow(Controller* controller, QWidget* parent) : QMainWindow(parent), controller_(controller) {
    setObjectName("mainWindow");
    resize(1360, 860);
    flows_ = new Flows(controller, this);
    scene_ = new CanvasScene(controller, this);
    view_ = new CanvasView(scene_, this);
    setCentralWidget(view_);

    buildActions();
    buildPanels();

    // The canvas reports clicks; the work happens after the item's event handler has returned.
    connect(scene_, &CanvasScene::freeSrcPadClicked, flows_, [this](const QString& e, const QString& id, const QPoint&) { flows_->fromFreePad(e, id); }, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::linkMenuRequested, flows_, &Flows::showLinkMenu, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::elementMenuRequested, flows_, &Flows::showElementMenu, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::elementActivated, flows_, &Flows::editProperties, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::elementMoved, controller_, &Controller::setPosition, Qt::QueuedConnection);
    connect(scene_, &CanvasScene::rebuilt, this, &MainWindow::onRebuilt);

    connect(controller_, &Controller::changed, scene_, &CanvasScene::scheduleRebuild);
    connect(controller_, &Controller::changed, this, &MainWindow::refreshState);
    connect(controller_, &Controller::logLine, log_, &LogPanel::append);
    connect(controller_, &Controller::notice, this, [this](const QString& text) {
        log_->append(text);
        statusBar()->showMessage(text, 10000);
    });
    refreshState();
}

void MainWindow::buildActions() {
    auto make = [this](const QString& name, const QString& text, const QKeySequence& key, auto slot) {
        auto* a = new QAction(text, this);
        a->setObjectName(name);
        if (!key.isEmpty()) a->setShortcut(key);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    auto* newPlan = make("actionNew", "Yeni", QKeySequence::New, [this] {
        if (confirmDiscard()) controller_->newPlan();
    });
    auto* open = make("actionOpen", "Aç", QKeySequence::Open, [this] {
        if (!confirmDiscard()) return;
        QString path = QFileDialog::getOpenFileName(this, "Pipeline aç", QString(), "Pipeline (*.conf)");
        if (!path.isEmpty()) openFile(path);
    });
    auto* save = make("actionSave", "Kaydet", QKeySequence::Save, [this] {
        if (controller_->filePath().isEmpty()) saveAs();
        else saveFile(QString());
    });
    auto* saveAs = make("actionSaveAs", "Farklı Kaydet", QKeySequence::SaveAs, [this] { this->saveAs(); });
    auto* update = make("actionUpdate", "Güncelle", QKeySequence(Qt::Key_F5), [this] { controller_->refresh(); });
    auto* run = make("actionRun", "Çalıştır", QKeySequence(Qt::CTRL | Qt::Key_R), [this] { toggleRun(); });
    auto* addSource = make("actionAddSource", "Kaynak ekle", QKeySequence(Qt::CTRL | Qt::Key_E), [this] { flows_->chooseSource(); });
    auto* fit = make("actionFit", "Sığdır", QKeySequence(Qt::CTRL | Qt::Key_0), [this] { view_->fitAll(); });
    auto* zoomIn = make("actionZoomIn", "Yakınlaştır", QKeySequence::ZoomIn, [this] { view_->zoomBy(1.25); });
    auto* zoomOut = make("actionZoomOut", "Uzaklaştır", QKeySequence::ZoomOut, [this] { view_->zoomBy(0.8); });
    auto* quit = make("actionQuit", "Çıkış", QKeySequence::Quit, [this] { close(); });

    QMenu* file = menuBar()->addMenu("Dosya");
    file->addActions({newPlan, open, save, saveAs});
    file->addSeparator();
    file->addActions({update, run});
    file->addSeparator();
    file->addAction(quit);
    QMenu* viewMenu = menuBar()->addMenu("Görünüm");
    viewMenu->addActions({fit, zoomIn, zoomOut});

    QToolBar* bar = addToolBar("Araçlar");
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
    sources_ = new SourcePanel(controller_, this);
    auto* left = new QDockWidget("Kaynaklar", this);
    left->setObjectName("sourceDock");
    left->setFeatures(QDockWidget::NoDockWidgetFeatures);
    left->setWidget(sources_);
    left->setMinimumWidth(300);
    addDockWidget(Qt::LeftDockWidgetArea, left);
    connect(sources_, &SourcePanel::sourceChosen, flows_, &Flows::addSource);

    log_ = new LogPanel(this);
    updates_ = new UpdatePanel(controller_, this);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName("bottomTabs");
    tabs_->addTab(log_, "Günlük");
    tabs_->addTab(updates_, "Güncelleme");
    tabs_->setCurrentWidget(updates_);
    auto* bottom = new QDockWidget("Paneller", this);
    bottom->setObjectName("bottomDock");
    bottom->setFeatures(QDockWidget::NoDockWidgetFeatures);
    bottom->setTitleBarWidget(new QWidget(bottom));  // the tabs are the title
    bottom->setWidget(tabs_);
    bottom->setMinimumHeight(170);
    addDockWidget(Qt::BottomDockWidgetArea, bottom);
    resizeDocks({bottom}, {200}, Qt::Vertical);

    state_ = new QLabel(this);
    state_->setObjectName("stateLabel");
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 0);
    progress_->setFixedWidth(120);
    progress_->setTextVisible(false);
    progress_->hide();
    command_ = new QLabel(this);
    command_->setObjectName("commandLabel");
    command_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    command_->setStyleSheet("font-family: 'DejaVu Sans Mono'; color: " + Theme::muted().name() + ";");
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(state_);
    statusBar()->addWidget(progress_);
    statusBar()->addPermanentWidget(command_);
}

void MainWindow::refreshState() {
    auto act = [this](const char* name) { return findChild<QAction*>(name); };
    bool busy = controller_->busy();
    bool hasPlan = !controller_->plan().empty();

    act("actionNew")->setEnabled(!busy);
    act("actionOpen")->setEnabled(!busy);
    act("actionAddSource")->setEnabled(!busy);
    act("actionSave")->setEnabled(controller_->canSave());
    act("actionSaveAs")->setEnabled(controller_->canSave());
    act("actionUpdate")->setEnabled(!busy && hasPlan);
    act("actionRun")->setText(controller_->running() ? "Durdur" : "Çalıştır");
    act("actionRun")->setEnabled(controller_->canRun() || controller_->running());

    const UpdateResult& r = controller_->result();
    QString text;
    QString colour = Theme::text().name();
    if (controller_->updating()) text = "Güncelleniyor…";
    else if (controller_->running()) text = "Çalışıyor…";
    else if (!hasPlan) text = "Boş tuval: soldan bir kaynak ekle";
    else if (r.valid() && !r.ok()) text = "Güncelleme başarısız: Kaydet ve Çalıştır kapalı", colour = Theme::error().name();
    else if (r.valid() && !r.complete) text = "Pipeline tamamlanmadı (boşta src pad var): Kaydet açık, Çalıştır kapalı", colour = Theme::warning().name();
    else if (r.valid()) text = "Pipeline tamamlandı", colour = Theme::accent().name();
    state_->setText(text);
    state_->setStyleSheet("color: " + colour + ";");
    progress_->setVisible(controller_->updating());
    command_->setText(controller_->launchCommand());

    QString name = controller_->filePath().isEmpty() ? "adsız" : QFileInfo(controller_->filePath()).fileName();
    setWindowTitle("GstPipeStudio – " + name + (controller_->dirty() ? " *" : ""));
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
    controller_->open(path);
}

bool MainWindow::saveFile(const QString& path) {
    QString error;
    if (controller_->save(path, &error)) return true;
    statusBar()->showMessage(error, 10000);
    log_->append(error);
    return false;
}

void MainWindow::saveAs() {
    QString path = QFileDialog::getSaveFileName(this, "Farklı kaydet", controller_->filePath(), "Pipeline (*.conf)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".conf")) path += ".conf";
    saveFile(path);
}

void MainWindow::toggleRun() {
    if (controller_->running()) controller_->stopRun();
    else controller_->run();
}

bool MainWindow::confirmDiscard() {
    if (!controller_->dirty() || controller_->plan().empty()) return true;
    auto answer = QMessageBox::question(this, "Kaydedilmemiş değişiklik", "Kaydedilmemiş değişiklikler silinecek. Devam edilsin mi?",
                                        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    return answer == QMessageBox::Yes;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (controller_->running()) controller_->stopRun();
    if (confirmDiscard()) event->accept();
    else event->ignore();
}
