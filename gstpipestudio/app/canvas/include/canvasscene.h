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

    // Locked while an update runs (UI_RULES 3.1): clicks, menus and dragging do nothing. Pan and zoom belong to the view.
    void setLocked(bool locked);
    bool locked() const { return locked_; }
    // Dimmed after a cancelled validation: the pad information on screen is from before the last edit.
    void setStale(bool stale);

    // Called by the items.
    void padClicked(const PadView& pad, Qt::MouseButton button, const QPoint& screenPos);
    void padDroppedOnPad(const PadView& from, const PadView& to);
    void elementContext(const QString& element, const QPoint& screenPos);
    void elementDoubleClicked(const QString& element);
    void elementDragged(const QString& element, const QPointF& pos);
    void linkClicked(const QString& srcElement, const QString& srcIdentity);
    void linkContext(const QString& srcElement, const QString& srcIdentity, const QPoint& screenPos);
    void updateLinks();

public slots:
    void scheduleRebuild();  // coalesces many changes into one rebuild
    void rebuild();

signals:
    void freeSrcPadClicked(const QString& element, const QString& identity, const QPoint& screenPos);
    void linkClickedSignal(const QString& srcElement, const QString& srcIdentity);
    void linkMenuRequested(const QString& srcElement, const QString& srcIdentity, const QPoint& screenPos);
    void elementMenuRequested(const QString& element, const QPoint& screenPos);
    void elementActivated(const QString& element);
    void elementMoved(const QString& element, double x, double y);
    void elementSpliced(const QString& element, const QString& linkSrcElement, const QString& linkSrcPad);
    void rebuilt();

private:
    static QString key(const QString& element, const QString& identity, bool isSrc);

    Controller* controller_;
    QHash<QString, ElementItem*> elements_;
    QHash<QString, PadItem*> pads_;
    QList<LinkItem*> links_;
    bool pending_ = false;
    bool locked_ = false;
    bool stale_ = false;
};
