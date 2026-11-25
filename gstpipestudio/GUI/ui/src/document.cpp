// document.cpp – see document.h.
#include "document.h"

#include <QHBoxLayout>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "canvasscene.h"
#include "canvasview.h"
#include "controller.h"
#include "drawer.h"
#include "flows.h"
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

    // What happens inside the document; the window wires only what concerns it.
    Flows* flows = flows_;
    Controller* c = controller_;
    CanvasScene* scene = scene_;
    connect(scene, &CanvasScene::freeSrcPadClicked, flows, [flows](const QString& e, const QString& id, const QPoint&) { flows->fromFreePad(e, id); }, Qt::QueuedConnection);
    connect(scene, &CanvasScene::linkClickedSignal, flows, &Flows::editCaps, Qt::QueuedConnection);
    connect(scene, &CanvasScene::linkMenuRequested, flows, &Flows::showLinkMenu, Qt::QueuedConnection);
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
    connect(c, &Controller::logLine, log_, &LogPanel::append);
    connect(c, &Controller::notice, log_, &LogPanel::append);
}

// The widgets live in the window's stacks and docks; they go with the document (the window deletes its documents
// before its own widgets). The caller's controller stays.
Document::~Document() {
    disconnect(scene_, nullptr, nullptr, nullptr);  // while it goes, it reports its items leaving the selection
    for (QObject* part : std::initializer_list<QObject*>{flows_, centre_, log_, updates_, telemetry_, propsTab_, capsTab_, indicator_, scene_})
        delete part;
}

QList<QWidget*> Document::panelPages() const { return {log_, updates_, telemetry_, propsTab_, capsTab_}; }

// An untitled tab a file is opening in reads the file's name already.
QString Document::title() const {
    const SaveTarget& target = controller_->target();
    return target.needsDestination() && controller_->busy() && !opening_.isEmpty() ? opening_ : target.title();
}

QString Document::path() const { return controller_->target().path(); }

bool Document::pristine() const {
    return controller_->target().needsDestination() && controller_->plan().empty() && !controller_->modified() && !controller_->busy();
}

bool Document::showsFile(const QString& file) const { return controller_->target().sessionKey() == ProjectConfTarget::keyFor(file); }

bool Document::showsTemplate(const QString& name) const {
    return controller_->target().sessionKey() == LibraryTemplateTarget::keyFor(name);
}

// The view fits the pipeline when its elements change, and once when a file has opened.
void Document::rebuilt() {
    QStringList names;
    for (const auto& e : controller_->plan().elements) names << QString::fromStdString(e.name);
    const bool changed = names != lastNames_;
    lastNames_ = names;
    if (names.isEmpty()) return;
    if ((changed && !fitNext_) || (fitNext_ && !controller_->busy())) {
        if (fitNext_ && !controller_->busy()) fitNext_ = false;
        QTimer::singleShot(0, view_, &CanvasView::fitAll);
    }
}
