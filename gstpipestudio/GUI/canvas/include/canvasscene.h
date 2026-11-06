// canvasscene.h – the scene drawn from the Controller's views. It is rebuilt from scratch on every change
// (a pipeline has a handful of elements), and reports what the user clicked as signals.
#pragma once

#include <QGraphicsScene>
#include <QHash>
#include <QJsonObject>
#include <QPair>
#include <QPointF>
#include <QSet>
#include <QStringList>

namespace gps { class Pipeline; }
class Controller;
class ElementItem;
class LinkItem;
class PadItem;
struct PadView;

class CanvasScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit CanvasScene(Controller* controller, QObject* parent = nullptr);

    const gps::Pipeline& pipeline() const;
    ElementItem* element(const QString& name) const { return elements_.value(name); }
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
    void elementContext(const QString& element, const QPoint& screenPos);
    void elementDoubleClicked(const QString& element);
    void elementDragged(const QString& element, const QPointF& pos);
    void linkClicked(const QString& srcElement, const QString& srcIdentity);
    void linkContext(const QString& srcElement, const QString& srcIdentity, const QPoint& screenPos);
    void updateLinks();
    void setConnectedMoveEnabled(bool enabled) { connectedMoveEnabled_ = enabled; }
    void beginConnectedMove(const QString& name);
    void followConnectedMove(const QString& name, const QPointF& position);
    void finishConnectedMove();

public slots:
    void scheduleRebuild();  // coalesces many changes into one rebuild
    void rebuild();
    void applyTelemetry(const QJsonObject& sample);

signals:
    void freeSrcPadClicked(const QString& element, const QString& identity, const QPoint& screenPos);
    void linkClickedSignal(const QString& srcElement, const QString& srcIdentity);
    void linkMenuRequested(const QString& srcElement, const QString& srcIdentity, const QPoint& screenPos);
    void elementMenuRequested(const QString& element, const QPoint& screenPos);
    void canvasContextMenuRequested(const QPoint& screenPos, const QPointF& scenePos);
    void elementActivated(const QString& element);
    void elementMoved(const QString& element, double x, double y);
    void elementsMoved(const QList<QPair<QString, QPointF>>& positions);
    void elementSpliced(const QString& element, const QString& linkSrcElement, const QString& linkSrcPad);
    void rebuilt();

protected:
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

private:
    bool connectedMoveEnabled_ = false, movingConnected_ = false;
    QString movingAnchor_;
    QPointF anchorStart_;
    QHash<QString, QPointF> connectedStart_;
    static QString key(const QString& element, const QString& identity, bool isSrc);

    Controller* controller_;
    QHash<QString, ElementItem*> elements_;
    QHash<QString, PadItem*> pads_;
    QList<LinkItem*> links_;
    QSet<QString> selectedElements_;
    bool hasExplicitSelection_ = false;
    class ConnectionPreview* dragWire_ = nullptr;
    bool pending_ = false;
    bool locked_ = false;
    bool stale_ = false;
    QJsonObject telemetry_;
};
