// document.cpp – see document.h.
#include "document.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "canvasscene.h"
#include "canvasview.h"
#include "controller.h"
#include "documentbackup.h"
#include "drawer.h"
#include "flows.h"
#include "linkitem.h"
#include "logpanel.h"
#include "savetarget.h"
#include "setuppage.h"
#include "telemetrypanel.h"
#include "updatepanel.h"
#include "validationindicator.h"

namespace {
QWidget* holder(const char* name, QWidget* parent) {
    auto* page = new QWidget(parent);
    page->setObjectName(name);
    new QVBoxLayout(page);
    return page;
}
}  // namespace

Document::Document(AppServices& services, Controller* controller, QWidget* window)
    : QObject(window), controller_(controller ? controller : new Controller(services, this)) {
    drawer_ = new Drawer(window);
    flows_ = new Flows(controller_, drawer_, window);
    scene_ = new CanvasScene(controller_, this);
    view_ = new CanvasView(scene_, window);

    // The drawer slides in on the right side of the canvas; the canvas keeps its own scroll position.
    auto* canvasPage = new QWidget(window);
    auto* canvasRow = new QHBoxLayout(canvasPage);
    canvasRow->setContentsMargins(0, 0, 0, 0);
    canvasRow->setSpacing(0);
    canvasRow->addWidget(view_, 1);
    canvasRow->addWidget(drawer_);
    setup_ = new SetupPage(window);
    centre_ = new QStackedWidget(window);
    centre_->setObjectName("centre");
    centre_->addWidget(canvasPage);
    centre_->addWidget(setup_);

    log_ = new LogPanel(window);
    updates_ = new UpdatePanel(controller_, window);
    telemetry_ = new TelemetryPanel(controller_, window);
    propsTab_ = holder("propsTab", window);
    capsTab_ = holder("capsTab", window);
    indicator_ = new ValidationIndicator(controller_, window);
    backup_ = new DocumentBackup(services.backups(), controller_, this);

    // What happens inside the document; the window wires only what concerns it.
    Flows* flows = flows_;
    Controller* c = controller_;
    CanvasScene* scene = scene_;
    connect(scene, &CanvasScene::freeSrcPadClicked, flows, [flows](const QString& e, const QString& id, const QPoint&) { flows->fromFreePad(e, id); }, Qt::QueuedConnection);
    connect(scene, &CanvasScene::linkClickedSignal, flows, &Flows::editCaps, Qt::QueuedConnection);
    connect(scene, &CanvasScene::templateActivated, flows, &Flows::editTemplateParams);
    connect(scene, &CanvasScene::elementActivated, flows, &Flows::editProperties, Qt::QueuedConnection);
    connect(scene, &CanvasScene::elementsMoved, c, &Controller::setPositions, Qt::QueuedConnection);
    connect(scene, &CanvasScene::elementSpliced, c, [scene, c](const QString& element, const QString& linkSrc, const QString& linkPad) {
        const QStringList sel = scene->selectedElementNames();
        if (sel.contains(element) && sel.size() > 1) c->spliceElements(sel, linkSrc, linkPad);
        else c->spliceElement(element, linkSrc, linkPad);
    }, Qt::QueuedConnection);
    connect(scene, &CanvasScene::rebuilt, this, &Document::rebuilt);
    connect(setup_, &SetupPage::retry, &services.environment(), &EnvironmentService::check);
    connect(c, &Controller::changed, scene, &CanvasScene::scheduleRebuild);
    connect(c, &Controller::changed, this, &Document::showState);
    connect(c, &Controller::logLine, log_, &LogPanel::append);
    connect(c, &Controller::notice, log_, &LogPanel::append);
}

// The widgets live in the window's stacks and docks; they go with the document (the window deletes its documents
// before its own widgets). The caller's controller stays.
Document::~Document() {
    disconnect(scene_, nullptr, nullptr, nullptr);  // while it goes, it reports its items leaving the selection
    for (QObject* part : std::initializer_list<QObject*>{backup_, flows_, centre_, log_, updates_, telemetry_, propsTab_, capsTab_, indicator_, scene_})
        delete part;
}

QList<QWidget*> Document::panelPages() const { return {log_, updates_, telemetry_, propsTab_, capsTab_}; }

// ---- the selection

QStringList Document::selection() const { return scene_->selectedElementNames(); }

bool Document::hasSelectedItems() const { return !scene_->selectedItems().isEmpty(); }

void Document::selectAll() {
    for (QGraphicsItem* item : scene_->items()) {
        if (item->flags() & QGraphicsItem::ItemIsSelectable) item->setSelected(true);
    }
}

void Document::copySelection() const {
    const gps::Graph clip = controller_->copySubplan(selection());
    if (QClipboard* clipboard = QGuiApplication::clipboard(); clipboard && !clip.elements.empty())
        clipboard->setText(QString::fromStdString(clip.toConf()));
}

void Document::cutSelection() {
    copySelection();
    controller_->deleteSelection(selection());
}

void Document::paste() {
    QClipboard* clipboard = QGuiApplication::clipboard();
    gps::Graph clip;
    std::string error;
    if (!clipboard || !gps::Graph::fromConf(clipboard->text().toStdString(), &clip, &error)) return;
    const QStringList created = controller_->pasteSubplan(clip);
    if (!created.isEmpty()) scene_->setSelectedElements(created);
}

void Document::duplicateSelection() {
    const QStringList created = controller_->duplicateElements(selection());
    if (!created.isEmpty()) scene_->setSelectedElements(created);
}

void Document::deleteSelection() {
    const QStringList elements = selection();
    QList<gps::LinkRef> links;
    for (QGraphicsItem* item : scene_->selectedItems()) {
        if (auto* link = dynamic_cast<LinkItem*>(item)) links.append({link->view().src});
    }
    const bool lonelySource = elements.size() == 1 && links.isEmpty() && controller_->outline().collapsedTemplate(elements.first()).isEmpty() &&
                              controller_->view(elements.first()).isSource;
    controller_->deleteSelection(elements, links, !lonelySource);
}

void Document::alignSelection() { controller_->niceAlign(selection()); }

// An untitled tab a file is opening in reads the file's name already.
QString Document::title() const {
    const SaveTarget& target = controller_->target();
    const QString title = target.needsDestination() && controller_->busy() && !opening_.isEmpty() ? opening_ : target.title();
    return fileDeleted_ ? title + " (deleted)" : title;
}

QString Document::path() const { return controller_->target().path(); }

// A tab holding a backup it could not load is not a blank: that backup is still its.
bool Document::pristine() const {
    return controller_->target().needsDestination() && controller_->plan().empty() && !controller_->modified() && !controller_->busy() &&
           !backup_->held();
}

void Document::setFileDeleted(bool deleted) {
    fileDeleted_ = deleted;
    if (deleted) controller_->markModified();
}

bool Document::shows(const QString& sessionKey) const { return controller_->intendedTarget().sessionKey() == sessionKey; }

// While editing is blocked the canvas is locked and the pages wait; a cancelled validation greys it; a run dims it.
void Document::showState() {
    const bool blocked = controller_->editingBlocked();
    scene_->setLocked(blocked);
    scene_->setStale(controller_->notValidated());
    view_->setDimmed(controller_->running());
    drawer_->setBusy(blocked);
    flows_->setBusy(blocked);
}

// The view fits the pipeline when its elements change, and once when a file has opened.
void Document::rebuilt() {
    const QStringList names = fromStd(controller_->plan().elementNames());
    const bool changed = names != lastNames_;
    lastNames_ = names;
    if (names.isEmpty()) return;
    if ((changed && !fitNext_) || (fitNext_ && !controller_->busy())) {
        if (fitNext_ && !controller_->busy()) fitNext_ = false;
        QTimer::singleShot(0, view_, &CanvasView::fitAll);
    }
}
