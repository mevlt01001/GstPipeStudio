// panels.h – the window's panels as dock widgets: Log, Validation, Live metrics, Properties and Caps under the canvas,
// the Explorer left of it. The standard way: the user moves, tabs, splits, floats and closes them, and View > Panels
// brings each one back. The window remembers where they are; Reset layout puts each back on its own side, tabbed with
// the others there. The one owner of showing a panel: flows and menus ask it, nobody touches a dock or a tab directly.
#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <Qt>

class QAction;
class QDockWidget;
class QMainWindow;
class QWidget;

class Panels : public QObject {
    Q_OBJECT
public:
    explicit Panels(QMainWindow* window);

    // The next tab on its side (under the canvas, or left of it). A panel that holds a form asks for room: shown, it is
    // at least that tall.
    void add(QWidget* panel, const QString& title, int room = 0, Qt::DockWidgetArea side = Qt::BottomDockWidgetArea);
    void show(QWidget* panel);                 // open, in front of its tab group, with its room
    bool showing(QWidget* panel) const;        // open and in front
    QList<QAction*> toggles() const;           // one checkable action per panel, for View > Panels
    void reset(QWidget* front);                // every panel docked on its side, tabbed there, front in front
    QDockWidget* dock(QWidget* panel) const;

private:
    QMainWindow* window_;
    QList<QDockWidget*> docks_;
    QHash<QDockWidget*, int> room_;
    QHash<QDockWidget*, Qt::DockWidgetArea> side_;
    QDockWidget* firstOn(Qt::DockWidgetArea side) const;
};
