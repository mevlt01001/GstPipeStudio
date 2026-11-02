// canvasscene.h – the scene drawn from the Controller's views. It is rebuilt from scratch on every change
// (a pipeline has a handful of elements), and reports what the user clicked as signals.
#pragma once

#include <QGraphicsScene>
#include <QHash>

class Controller;
class ElementItem;
class LinkItem;
class PadItem;
struct PadView;

class CanvasScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit CanvasScene(Controller* controller, QObject* parent = nullptr);

    ElementItem* element(const QString& name) const { return elements_.value(name); }
    PadItem* pad(const QString& element, const QString& identity, bool isSrc) const;
    const QList<LinkItem*>& links() const { return links_; }

    // Called by the items.
    void padClicked(const PadView& pad, Qt::MouseButton button, const QPoint& screenPos);
    void elementContext(const QString& element, const QPoint& screenPos);
    void elementDoubleClicked(const QString& element);
    void elementDragged(const QString& element, const QPointF& pos);
    void linkContext(const QString& srcElement, const QString& srcIdentity, const QPoint& screenPos);
    void updateLinks();

public slots:
    void scheduleRebuild();  // coalesces many changes into one rebuild
    void rebuild();

signals:
    void freeSrcPadClicked(const QString& element, const QString& identity, const QPoint& screenPos);
    void linkMenuRequested(const QString& srcElement, const QString& srcIdentity, const QPoint& screenPos);
    void elementMenuRequested(const QString& element, const QPoint& screenPos);
    void elementActivated(const QString& element);
    void elementMoved(const QString& element, double x, double y);
    void rebuilt();

private:
    static QString key(const QString& element, const QString& identity, bool isSrc);

    Controller* controller_;
    QHash<QString, ElementItem*> elements_;
    QHash<QString, PadItem*> pads_;
    QList<LinkItem*> links_;
    bool pending_ = false;
};
