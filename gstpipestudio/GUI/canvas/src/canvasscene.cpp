// canvasscene.cpp
#include "canvasscene.h"

#include <QGraphicsPathItem>
#include <QGraphicsSceneContextMenuEvent>
#include <QJsonArray>
#include <QPainterPath>
#include <QPen>
#include <QTimer>
#include <cmath>

#include "controller.h"
#include "elementitem.h"
#include "linkitem.h"
#include "paditem.h"
#include "theme.h"

CanvasScene::CanvasScene(Controller* controller, QObject* parent) : QGraphicsScene(parent), controller_(controller) {
    setSceneRect(-2000, -2000, 4000, 4000);
    connect(controller_, &Controller::telemetryReceived, this, &CanvasScene::applyTelemetry);
}

void CanvasScene::applyTelemetry(const QJsonObject& sample) {
    telemetry_ = sample;
    QSet<ElementItem*> reset;
    for (ElementItem* item : std::as_const(elements_)) {
        if (!reset.contains(item)) { reset.insert(item); item->clearTelemetry(); }
    }
    if (!controller_->monitoring()) return;
    auto number = [](const QJsonObject& object, const char* key) {
        const QJsonValue value = object.value(key);
        return value.isDouble() ? value.toDouble() : -1.0;
    };
    for (const QJsonValue& value : sample.value("elements").toArray()) {
        const QJsonObject object = value.toObject();
        if (ElementItem* item = elements_.value(object.value("element").toString()))
            item->mergeTelemetry(number(object, "processingMs"), number(object, "dropRatePercent"), object.value("timingKind").toString() == "source-interval");
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
        if (auto* el = dynamic_cast<ElementItem*>(item)) {
            if (el->view().isCustomGroup && !el->view().groupMembers.isEmpty()) {
                for (const QString& m : el->view().groupMembers) {
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

void CanvasScene::scheduleRebuild() {
    if (pending_) return;
    pending_ = true;
    QTimer::singleShot(0, this, &CanvasScene::rebuild);
}

void CanvasScene::rebuild() {
    pending_ = false;
    dragWire_ = nullptr;
    if (!hasExplicitSelection_ && !elements_.isEmpty()) {
        const QStringList current = selectedElementNames();
        selectedElements_ = QSet<QString>(current.begin(), current.end());
    }
    hasExplicitSelection_ = false;

    clear();
    elements_.clear();
    pads_.clear();
    links_.clear();

    QSet<QString> builtCollapsedGroups;
    for (const gps::Element& e : pipeline().graph().elements) {
        QString name = QString::fromStdString(e.name);
        QString grp = QString::fromStdString(e.group);
        if (!grp.isEmpty() && !controller_->isGroupExpanded(grp)) {
            if (builtCollapsedGroups.contains(grp)) continue;
            builtCollapsedGroups.insert(grp);
            ElementView gv = controller_->groupView(grp);
            auto* item = new ElementItem(gv);
            item->setPos(e.x, e.y);
            item->setFlag(QGraphicsItem::ItemIsMovable, !locked_);
            item->setOpacity(stale_ ? 0.5 : 1.0);
            addItem(item);
            bool anySelected = false;
            for (const QString& m : gv.groupMembers) {
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
    for (const LinkView& l : controller_->linkViews()) {
        if (elements_.value(l.srcElement) && elements_.value(l.srcElement) == elements_.value(l.sinkElement)) {
            continue;  // internal link inside a collapsed custom element
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
    emit rebuilt();
}

void CanvasScene::updateLinks() {
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
        dragWire_ = new QGraphicsPathItem;
        QPen pen(Theme::accentHover(), 2.5, Qt::DashLine);
        dragWire_->setPen(pen);
        dragWire_->setZValue(3.0);
        addItem(dragWire_);
    }
    QPointF a = startPad->center();
    QPointF b = scenePos;
    double dir = from.isSrc ? 1.0 : -1.0;
    double reach = qBound(30.0, std::abs(b.x() - a.x()) / 2.0, 150.0);
    QPainterPath path(a);
    path.cubicTo(a + QPointF(dir * reach, 0), b - QPointF(dir * reach, 0), b);
    dragWire_->setPath(path);
}

void CanvasScene::padDragEnded() {
    if (dragWire_) {
        removeItem(dragWire_);
        delete dragWire_;
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
        if (!selNames.contains(element)) selNames = controller_->expandSelectionWithGroups({element});
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
