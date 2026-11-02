// canvasscene.cpp
#include "canvasscene.h"

#include <QTimer>

#include "controller.h"
#include "elementitem.h"
#include "linkitem.h"
#include "paditem.h"

CanvasScene::CanvasScene(Controller* controller, QObject* parent) : QGraphicsScene(parent), controller_(controller) {}

QString CanvasScene::key(const QString& element, const QString& identity, bool isSrc) {
    return element + "|" + identity + (isSrc ? "|src" : "|sink");
}

PadItem* CanvasScene::pad(const QString& element, const QString& identity, bool isSrc) const {
    return pads_.value(key(element, identity, isSrc));
}

void CanvasScene::scheduleRebuild() {
    if (pending_) return;
    pending_ = true;
    QTimer::singleShot(0, this, &CanvasScene::rebuild);
}

void CanvasScene::rebuild() {
    pending_ = false;
    clear();
    elements_.clear();
    pads_.clear();
    links_.clear();

    for (const gps::PlanElement& e : controller_->plan().elements) {
        auto* item = new ElementItem(controller_->view(QString::fromStdString(e.name)));
        item->setPos(e.x, e.y);
        addItem(item);
        elements_.insert(item->view().name, item);
        for (PadItem* p : item->pads()) pads_.insert(key(p->view().element, p->view().identity, p->view().isSrc), p);
    }
    for (const LinkView& l : controller_->linkViews()) {
        PadItem* from = pad(l.srcElement, l.srcIdentity, true);
        PadItem* to = pad(l.sinkElement, l.sinkIdentity, false);
        if (!from || !to) continue;  // the element's info is not cached yet; the next rebuild draws it
        auto* link = new LinkItem(l, from, to);
        addItem(link);
        links_ << link;
    }
    setSceneRect(itemsBoundingRect().adjusted(-400, -300, 400, 300));
    emit rebuilt();
}

void CanvasScene::updateLinks() {
    for (LinkItem* l : links_) l->updatePath();
}

void CanvasScene::padClicked(const PadView& pad, Qt::MouseButton button, const QPoint& screenPos) {
    if (button == Qt::LeftButton && pad.isSrc && pad.state != PadView::Linked) {
        emit freeSrcPadClicked(pad.element, pad.identity, screenPos);
    } else if (button == Qt::RightButton && pad.link >= 0) {
        // A linked pad of either side belongs to a link; its menu is the link's menu.
        const gps::PlanLink& l = controller_->plan().links[pad.link];
        emit linkMenuRequested(QString::fromStdString(l.srcElement), QString::fromStdString(l.srcPad), screenPos);
    }
}

void CanvasScene::elementContext(const QString& element, const QPoint& screenPos) { emit elementMenuRequested(element, screenPos); }
void CanvasScene::elementDoubleClicked(const QString& element) { emit elementActivated(element); }
void CanvasScene::elementDragged(const QString& element, const QPointF& pos) { emit elementMoved(element, pos.x(), pos.y()); }
void CanvasScene::linkContext(const QString& srcElement, const QString& srcIdentity, const QPoint& screenPos) {
    emit linkMenuRequested(srcElement, srcIdentity, screenPos);
}
