// canvasscene.cpp
#include "canvasscene.h"
#include "connectionpreview.h"

#include <QCursor>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPath>
#include <QPen>
#include <QSignalBlocker>
#include <QTimer>
#include <QVariantAnimation>
#include <algorithm>
#include <cmath>
#include <tuple>

#include "controller.h"
#include "elementitem.h"
#include "linkitem.h"
#include "paditem.h"
#include "templateframeitem.h"
#include "templatepeek.h"
#include "templates.h"
#include "theme.h"

CanvasScene::CanvasScene(Controller* controller, QObject* parent) : QGraphicsScene(parent), controller_(controller) {
    setSceneRect(-2000, -2000, 4000, 4000);
    connect(controller_, &Controller::telemetryReceived, this, &CanvasScene::applyTelemetry);
    connect(&controller_->canvasLayout(), &CanvasLayout::applied, this, [this](const QHash<QString, QPointF>& from) {
        animateFrom_ = from;
        scheduleRebuild();
    });
}

// The boxes a nice alignment moved slide from where they were to where it put them; the links follow.
void CanvasScene::slide() {
    QList<std::tuple<ElementItem*, QPointF, QPointF>> moves;
    QSet<ElementItem*> seen;
    for (ElementItem* item : std::as_const(elements_)) {
        if (!item || seen.contains(item) || !animateFrom_.contains(item->view().name)) continue;
        seen.insert(item);
        const QPointF start = animateFrom_.value(item->view().name), end = item->pos();
        if (start != end) moves.append({item, start, end});
    }
    animateFrom_.clear();
    if (moves.isEmpty()) return;
    slide_ = new QVariantAnimation(this);
    slide_->setDuration(260);
    slide_->setEasingCurve(QEasingCurve::OutCubic);
    slide_->setStartValue(0.0);
    slide_->setEndValue(1.0);
    connect(slide_, &QVariantAnimation::valueChanged, this, [this, moves](const QVariant& value) {
        const double t = value.toDouble();
        for (const auto& [item, start, end] : moves) item->setPos(start + (end - start) * t);
        updateLinks();
    });
    slide_->start(QAbstractAnimation::DeleteWhenStopped);
}

void CanvasScene::applyTelemetry(const gps::TelemetrySample& sample) {
    telemetry_ = sample;
    QSet<ElementItem*> reset;
    for (ElementItem* item : std::as_const(elements_)) {
        if (!reset.contains(item)) { reset.insert(item); item->clearTelemetry(); }
    }
    if (!controller_->monitoring()) return;
    for (const gps::ElementTelemetry& t : sample.elements) {
        if (ElementItem* item = elements_.value(QString::fromStdString(t.element)))
            item->mergeTelemetry(t.processingMs, t.dropRatePercent, t.ageMs);
    }
}

const gps::Pipeline& CanvasScene::pipeline() const { return controller_->pipeline(); }

QString CanvasScene::key(const QString& element, const QString& identity, bool isSrc) {
    return element + "|" + identity + (isSrc ? "|src" : "|sink");
}

PadItem* CanvasScene::pad(const QString& element, const QString& identity, bool isSrc) const {
    return pads_.value(key(element, identity, isSrc));
}

QStringList CanvasScene::selectedElementNames() const {
    QStringList names;
    QSet<QString> seen;
    for (QGraphicsItem* item : selectedItems()) {
        if (auto* frame = dynamic_cast<TemplateFrameItem*>(item)) {
            for (const QString& m : frame->view().members) {
                if (!seen.contains(m)) {
                    seen.insert(m);
                    names << m;
                }
            }
            continue;
        }
        if (auto* el = dynamic_cast<ElementItem*>(item)) {
            if (el->view().isTemplate && !el->view().templateMembers.isEmpty()) {
                for (const QString& m : el->view().templateMembers) {
                    if (!seen.contains(m)) {
                        seen.insert(m);
                        names << m;
                    }
                }
            } else if (!seen.contains(el->view().name)) {
                seen.insert(el->view().name);
                names << el->view().name;
            }
        }
    }
    return names;
}

void CanvasScene::setSelectedElements(const QStringList& names) {
    selectedElements_ = QSet<QString>(names.begin(), names.end());
    hasExplicitSelection_ = true;
    for (auto it = elements_.constBegin(); it != elements_.constEnd(); ++it) {
        if (it.value()) it.value()->setSelected(selectedElements_.contains(it.key()));
    }
}

QList<FrameGhost*> CanvasScene::ghosts() const {
    QList<FrameGhost*> out;
    for (const QPointer<FrameGhost>& g : ghosts_) {
        if (g && !g->done()) out << g;
    }
    return out;
}

void CanvasScene::scheduleRebuild() {
    if (pending_) return;
    pending_ = true;
    QTimer::singleShot(0, this, &CanvasScene::rebuild);
}

void CanvasScene::rebuild() {
    pending_ = false;
    if (slide_) slide_->stop();  // its items go away with the rebuild
    finishConnectedMove();
    dragWire_ = nullptr;
    if (!hasExplicitSelection_ && !elements_.isEmpty()) {
        const QStringList current = selectedElementNames();
        selectedElements_ = QSet<QString>(current.begin(), current.end());
    }
    hasExplicitSelection_ = false;

    QHash<QString, QRectF> closing;  // frames drawn now, in case they close
    for (auto it = frames_.cbegin(); it != frames_.cend(); ++it) closing[it.key()] = it.value()->frameRect();
    ghosts_.removeIf([this](const QPointer<FrameGhost>& g) {
        if (g && !g->done()) removeItem(g);  // kept for after the rebuild
        else if (g) delete g.data();
        return !g;
    });
    {
        // Deleting the items takes them out of the selection; nobody should hear about it half way through.
        const QSignalBlocker quiet(this);
        clear();
        elements_.clear();
        pads_.clear();
        links_.clear();
        frames_.clear();
    }

    QSet<QString> builtCollapsedGroups;
    for (const gps::Element& e : pipeline().graph().elements) {
        QString name = QString::fromStdString(e.name);
        QString grp = controller_->templateBox(name);
        if (!grp.isEmpty()) {
            if (builtCollapsedGroups.contains(grp)) continue;
            builtCollapsedGroups.insert(grp);
            ElementView gv = controller_->templateView(grp);
            auto* item = new ElementItem(gv);
            item->setPos(e.x, e.y);
            item->setFlag(QGraphicsItem::ItemIsMovable, !locked_);
            item->setOpacity(stale_ ? 0.5 : 1.0);
            addItem(item);
            bool anySelected = false;
            for (const QString& m : gv.templateMembers) {
                elements_.insert(m, item);
                if (selectedElements_.contains(m)) anySelected = true;
            }
            if (anySelected) item->setSelected(true);
            for (PadItem* p : item->pads()) pads_.insert(key(p->view().element, p->view().identity, p->view().isSrc), p);
        } else {
            auto* item = new ElementItem(controller_->view(name));
            item->setPos(e.x, e.y);
            item->setFlag(QGraphicsItem::ItemIsMovable, !locked_);
            item->setOpacity(stale_ ? 0.5 : 1.0);
            addItem(item);
            elements_.insert(item->view().name, item);
            if (selectedElements_.contains(name)) item->setSelected(true);
            for (PadItem* p : item->pads()) pads_.insert(key(p->view().element, p->view().identity, p->view().isSrc), p);
        }
    }
    // A template opening or closing is drawn gathered at its centre until the layout's answer slides it out of there.
    const auto& pin = controller_->canvasLayout().pending();
    if (pin && pin->gather) {
        for (const QString& box : controller_->canvasLayout().boxesOf(pin->elements)) {
            ElementItem* item = elements_.value(controller_->canvasLayout().anchorOf(box));
            if (item) item->setPos(pin->centre - QPointF(metrics::kBoxWidth, item->view().height()) / 2);
        }
    }
    // Expanded templates: a frame around their members, with the template's pads on its border. Inner frames first, so
    // a pad shown on several borders belongs to the outermost one, where its outside link attaches.
    QList<FrameView> shown;
    for (const QString& id : controller_->visibleFrames()) shown << controller_->frameView(id);
    std::sort(shown.begin(), shown.end(), [](const FrameView& a, const FrameView& b) { return a.depth > b.depth; });
    QSet<QString> nowShown;
    for (const FrameView& fv : shown) {
        auto* frame = new TemplateFrameItem(fv, this);
        addItem(frame);
        frames_.insert(fv.id, frame);
        nowShown.insert(fv.id);
        for (PadItem* p : frame->pads()) {
            const QString k = key(p->view().element, p->view().identity, p->view().isSrc);
            frame->setInner(p, pads_.value(k));
            pads_.insert(k, p);
        }
        frame->updateGeometry();
        if (!knownInstances_.contains(fv.id)) traceStarted_[fv.id].start();  // a template just made, not one just opened
        if (traceStarted_.contains(fv.id)) frame->trace(traceStarted_[fv.id].elapsed());
        if (selectedElements_.contains(fv.members.value(0)) && fv.members.size() > 1 &&
            std::all_of(fv.members.begin(), fv.members.end(), [&](const QString& m) { return selectedElements_.contains(m); }))
            frame->setSelected(true);
    }
    shownFrames_ = nowShown;
    traceStarted_.removeIf([](const auto& it) { return it.value().elapsed() > TemplateFrameItem::kTraceMs; });
    knownInstances_.clear();
    for (const gps::TemplateInstance& t : pipeline().graph().instances) knownInstances_.insert(QString::fromStdString(t.id));
    for (const QPointer<FrameGhost>& g : std::as_const(ghosts_)) addItem(g);
    for (auto it = closing.cbegin(); it != closing.cend(); ++it) {
        if (nowShown.contains(it.key())) continue;
        ElementItem* box = elements_.value(controller_->templateMembers(it.key()).value(0));
        const bool closed = box && box->view().isTemplate && box->view().groupId == it.key();  // else deleted, detached
        auto* ghost = new FrameGhost(it.value(), closed ? box->sceneBoundingRect() : it.value(), closed ? FrameGhost::Shrink : FrameGhost::Untrace);
        addItem(ghost);
        ghosts_ << ghost;
    }
    for (const LinkView& l : controller_->linkViews()) {
        if (elements_.value(l.srcElement) && elements_.value(l.srcElement) == elements_.value(l.sinkElement)) {
            continue;  // internal link inside a collapsed template
        }
        PadItem* from = pad(l.srcElement, l.srcIdentity, true);
        PadItem* to = pad(l.sinkElement, l.sinkIdentity, false);
        if (!from || !to) continue;  // the element's info is not cached yet; the next rebuild draws it
        auto* link = new LinkItem(l, from, to);
        link->setOpacity(stale_ ? 0.5 : 1.0);
        addItem(link);
        links_ << link;
    }
    if (controller_->monitoring()) applyTelemetry(telemetry_);
    else telemetry_ = {};
    setSceneRect(sceneRect().united(itemsBoundingRect().adjusted(-1000, -1000, 1000, 1000)));
    if (!animateFrom_.isEmpty()) slide();
    emit rebuilt();
}

void CanvasScene::updateLinks() {
    QList<TemplateFrameItem*> inner = frames_.values();
    std::sort(inner.begin(), inner.end(), [](TemplateFrameItem* a, TemplateFrameItem* b) { return a->view().depth > b->view().depth; });
    for (TemplateFrameItem* f : inner) f->updateGeometry();
    for (LinkItem* l : links_) l->updatePath();
    setSceneRect(sceneRect().united(itemsBoundingRect().adjusted(-1000, -1000, 1000, 1000)));
}

void CanvasScene::setLocked(bool locked) {
    locked_ = locked;
    for (ElementItem* e : std::as_const(elements_)) e->setFlag(QGraphicsItem::ItemIsMovable, !locked);
}

void CanvasScene::setStale(bool stale) {
    if (stale_ == stale) return;
    stale_ = stale;
    scheduleRebuild();
}

void CanvasScene::padClicked(const PadView& pad, Qt::MouseButton button, const QPoint& screenPos) {
    if (locked_) return;
    if (button == Qt::LeftButton && pad.isSrc && pad.state != PadView::Linked) {
        emit freeSrcPadClicked(pad.element, pad.identity, screenPos);
    } else if (button == Qt::RightButton && pad.link >= 0) {
        // A linked pad of either side belongs to a link; its menu is the link's menu.
        const gps::Connection& l = controller_->plan().links[pad.link];
        emit linkMenuRequested(QString::fromStdString(l.srcElement), QString::fromStdString(l.srcPad), screenPos);
    }
}

void CanvasScene::padDragMoved(const PadView& from, const QPointF& scenePos) {
    if (locked_) return;
    PadItem* startPad = pad(from.element, from.identity, from.isSrc);
    if (!startPad) return;
    if (!dragWire_) {
        dragWire_ = new ConnectionPreview;
        addItem(dragWire_);
    }
    dragWire_->setEndpoints(startPad->center(), scenePos, from.isSrc);
}

void CanvasScene::padDragEnded() {
    if (dragWire_) {
        dragWire_->retract();
        dragWire_ = nullptr;
    }
}

void CanvasScene::padDroppedOnPad(const PadView& from, const PadView& to) {
    padDragEnded();
    if (locked_) return;
    const PadView* src = from.isSrc ? &from : (to.isSrc ? &to : nullptr);
    const PadView* sink = !from.isSrc ? &from : (!to.isSrc ? &to : nullptr);
    if (!src || !sink) return;
    controller_->linkExisting(src->element, src->identity, sink->element, sink->templateName);
}

void CanvasScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
    if (locked_) return;
    // Only open the canvas background menu when no item at that position handled it.
    if (items(event->scenePos()).isEmpty()) {
        emit canvasContextMenuRequested(event->screenPos(), event->scenePos());
        event->accept();
        return;
    }
    QGraphicsScene::contextMenuEvent(event);
}

void CanvasScene::elementContext(const QString& element, const QPoint& screenPos) {
    if (!locked_) emit elementMenuRequested(element, screenPos);
}
void CanvasScene::elementDoubleClicked(const QString& element) {
    if (!locked_) emit elementActivated(element);
}
void CanvasScene::elementDragged(const QString& element, const QPointF& pos) {
    if (locked_) return;
    QList<QPair<QString, QPointF>> positions;
    QSet<ElementItem*> seenItems;
    for (ElementItem* item : std::as_const(elements_)) {
        if (!item || seenItems.contains(item)) continue;
        seenItems.insert(item);
        positions.append({item->view().name, item->pos()});
    }
    emit elementMoved(element, pos.x(), pos.y());
    emit elementsMoved(positions);
    if (this->element(element)) {
        QStringList selNames = selectedElementNames();
        if (!selNames.contains(element)) selNames = controller_->expandSelection({element});
        QSet<QString> draggedSet(selNames.begin(), selNames.end());
        QList<ElementItem*> draggedItems;
        for (const QString& n : selNames) {
            if (ElementItem* ei = this->element(n)) {
                if (!draggedItems.contains(ei)) draggedItems << ei;
            }
        }
        bool foundSplice = false;
        for (LinkItem* link : links_) {
            if (draggedSet.contains(link->view().srcElement) || draggedSet.contains(link->view().sinkElement)) continue;
            for (ElementItem* di : draggedItems) {
                if (di->collidesWithItem(link)) {
                    emit elementSpliced(element, link->view().srcElement, link->view().srcIdentity);
                    foundSplice = true;
                    break;
                }
            }
            if (foundSplice) break;
        }
    }
}
void CanvasScene::linkClicked(const QString& srcElement, const QString& srcIdentity) {
    if (!locked_) emit linkClickedSignal(srcElement, srcIdentity);
}
void CanvasScene::linkContext(const QString& srcElement, const QString& srcIdentity, const QPoint& screenPos) {
    if (!locked_) emit linkMenuRequested(srcElement, srcIdentity, screenPos);
}
void CanvasScene::frameContext(const QString& instanceId, const QPoint& screenPos) {
    if (!locked_) emit templateMenuRequested(instanceId, screenPos);
}
void CanvasScene::frameActivated(const QString& instanceId) {
    if (!locked_) emit templateActivated(instanceId);
}
void CanvasScene::frameMinimized(const QString& instanceId) {
    if (!locked_) controller_->setTemplateExpanded(instanceId, false);
}
void CanvasScene::frameExpanded(const QString& instanceId) {
    if (!locked_) controller_->setTemplateExpanded(instanceId, true);
}

void CanvasScene::peekTemplate(const QString& instanceId) {
    if (locked_ || views().isEmpty() || !pipeline().graph().instance(instanceId.toStdString())) return;
    TemplatePeek::show(gps::templates::extract(pipeline().graph(), instanceId.toStdString()), QCursor::pos(), views().front());
}

void CanvasScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    for (TemplateFrameItem* f : std::as_const(frames_)) f->pointer(event->scenePos());
    QGraphicsScene::mouseMoveEvent(event);
}

void CanvasScene::beginConnectedMove(const QString& name) {
    finishConnectedMove();
    if (!connectedMoveEnabled_ || locked_) return;
    ElementItem* anchor = element(name);
    if (!anchor) return;
    movingAnchor_ = name; anchorStart_ = anchor->pos();
    QSet<QString> component{name};
    QStringList pending{name};
    while (!pending.isEmpty()) {
        const QString current = pending.takeLast();
        for (const auto& link : pipeline().graph().links) {
            const QString src = QString::fromStdString(link.srcElement), sink = QString::fromStdString(link.sinkElement);
            const QString next = src == current ? sink : sink == current ? src : QString();
            if (!next.isEmpty() && !component.contains(next)) { component.insert(next); pending.append(next); }
        }
    }
    for (const QString& member : component) {
        auto* item = element(member);
        if (item && item != anchor && !item->isSelected()) connectedStart_.insert(member, item->pos());
    }
}
void CanvasScene::followConnectedMove(const QString& name, const QPointF& position) {
    if (movingConnected_ || name != movingAnchor_) return;
    movingConnected_ = true;
    const QPointF delta = position-anchorStart_;
    for (auto it = connectedStart_.cbegin(); it != connectedStart_.cend(); ++it)
        if (auto* item = element(it.key())) item->setPos(it.value()+delta);
    movingConnected_ = false;
}
void CanvasScene::finishConnectedMove() { movingAnchor_.clear(); connectedStart_.clear(); }
