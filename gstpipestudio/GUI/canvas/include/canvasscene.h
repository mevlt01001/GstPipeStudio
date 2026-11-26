// canvasscene.h – the scene drawn from the Controller's views. It is rebuilt from scratch on every change
// (a pipeline has a handful of elements), and reports what the user clicked as signals.
#pragma once

#include <QGraphicsScene>
#include <QElapsedTimer>
#include <QPointer>
#include <QVariantAnimation>
#include <QHash>
#include <QPair>
#include <QPointF>
#include <QSet>
#include <QStringList>

#include "graph.h"
#include "runtime.h"

namespace gps { class Pipeline; }
class Controller;
class ElementItem;
class LinkItem;
class PadItem;
class FrameGhost;
class TemplateFrameItem;
struct PadView;
struct ElementView;

class CanvasScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit CanvasScene(Controller* controller, QObject* parent = nullptr);

    const gps::Pipeline& pipeline() const;
    Controller* controller() const { return controller_; }
    ElementItem* element(const QString& name) const { return elements_.value(name); }
    TemplateFrameItem* frame(const QString& instanceId) const { return frames_.value(instanceId); }
    QList<TemplateFrameItem*> frames() const { return frames_.values(); }
    QList<FrameGhost*> ghosts() const;  // frames on their way out
    PadItem* pad(const gps::PadRef& ref) const;
    PadItem* pad(const QString& element, const QString& identity, bool isSrc) const;
    const QList<LinkItem*>& links() const { return links_; }
    QStringList selectedElementNames() const;
    void setSelectedElements(const QStringList& names);

    // Locked during playback, opening and cancellation. Preview permits editing; pan and zoom remain available.
    void setLocked(bool locked);
    bool locked() const { return locked_; }
    // Dimmed after a cancelled validation: the pad information on screen is from before the last edit.
    void setStale(bool stale);

    // Called by the items.
    void padClicked(const PadView& pad, Qt::MouseButton button, const QPoint& screenPos);
    void padDragMoved(const PadView& from, const QPointF& scenePos);
    void padDragEnded();
    void padDroppedOnPad(const PadView& from, const PadView& to);
    void padDroppedOnBox(const PadView& from, const ElementView& box);
    void elementContext(const QString& element, const QPoint& screenPos);
    void elementDoubleClicked(const QString& element);
    void elementDragged(const QString& element, const QPointF& pos);
    void linkClicked(const QString& srcElement, const QString& srcIdentity);
    void linkContext(const QString& srcElement, const QString& srcIdentity, const QPoint& screenPos);
    void frameContext(const QString& instanceId, const QPoint& screenPos);
    void frameActivated(const QString& instanceId);
    void frameMinimized(const QString& instanceId);
    void frameExpanded(const QString& instanceId);
    void peekTemplate(const QString& instanceId);  // a quick look inside a collapsed template, near the pointer
    void updateLinks();
    // With View > Move connected elements together, a drag moves the element's connected component.
    void beginConnectedMove(const QString& name);
    void followConnectedMove(const QString& name, const QPointF& position);
    void finishConnectedMove();

public slots:
    void scheduleRebuild();  // coalesces many changes into one rebuild
    void rebuild();
    void applyTelemetry(const gps::TelemetrySample& sample);

signals:
    void freeSrcPadClicked(const QString& element, const QString& identity, const QPoint& screenPos);
    void linkClickedSignal(const QString& srcElement, const QString& srcIdentity);
    void linkMenuRequested(const QString& srcElement, const QString& srcIdentity, const QPoint& screenPos);
    void elementMenuRequested(const QString& element, const QPoint& screenPos);
    void canvasContextMenuRequested(const QPoint& screenPos, const QPointF& scenePos);
    void elementActivated(const QString& element);
    void elementsMoved(const QList<QPair<QString, QPointF>>& positions);
    void elementSpliced(const QString& element, const QString& linkSrcElement, const QString& linkSrcPad);
    void templateMenuRequested(const QString& instanceId, const QPoint& screenPos);
    void templateActivated(const QString& instanceId);
    void rebuilt();

protected:
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;

private:
    bool movingConnected_ = false;
    QString movingAnchor_;
    QPointF anchorStart_;
    QHash<QString, QPointF> connectedStart_;

    Controller* controller_;
    QHash<QString, ElementItem*> elements_;
    QHash<QString, PadItem*> pads_;
    QHash<QString, TemplateFrameItem*> frames_;
    QSet<QString> shownFrames_;  // frames of the last rebuild
    QSet<QString> knownInstances_;  // templates of the last rebuild: a frame of one not among them is traced
    QHash<QString, QElapsedTimer> traceStarted_;  // template -> since when its border is being traced
    QList<QPointer<FrameGhost>> ghosts_;  // frames that just closed, shrinking: they outlive a rebuild
    QList<LinkItem*> links_;
    QSet<QString> selectedElements_;
    bool hasExplicitSelection_ = false;
    class ConnectionPreview* dragWire_ = nullptr;
    bool pending_ = false;
    bool locked_ = false;
    bool stale_ = false;
    gps::TelemetrySample telemetry_;
    QHash<QString, QPointF> animateFrom_;  // after a nice alignment: where the boxes were, to slide them from there
    QPointer<QVariantAnimation> slide_;
    void slide();
    void link(const gps::PadRef& a, const gps::PadRef& b);  // whichever is the src pad feeds the other
};
