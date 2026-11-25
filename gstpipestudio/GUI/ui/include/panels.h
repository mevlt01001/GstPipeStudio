// panels.h – the panels under the canvas (Log, Validation, Live metrics, Properties, Caps) as dock widgets, the
// standard way: the user moves, tabs, splits, floats and closes them, and View > Panels brings each one back. The
// window remembers where they are; Reset layout puts them back in one tabbed row under the canvas. The one owner of
// showing a panel: flows and menus ask it, nobody touches a dock or a tab directly.
#pragma once

#include <QHash>
#include <QList>
#include <QObject>

class QAction;
class QDockWidget;
class QMainWindow;
class QWidget;

class Panels : public QObject {
    Q_OBJECT
public:
    explicit Panels(QMainWindow* window);

    // The next tab of the row under the canvas. A panel that holds a form asks for room: shown, it is at least that tall.
    void add(QWidget* panel, const QString& title, int room = 0);
    void show(QWidget* panel);                 // open, in front of its tab group, with its room
    bool showing(QWidget* panel) const;        // open and in front
    QList<QAction*> toggles() const;           // one checkable action per panel, for View > Panels
    void reset(QWidget* front);                // every panel docked, tabbed under the canvas, front in front
    QDockWidget* dock(QWidget* panel) const;

private:
    QMainWindow* window_;
    QList<QDockWidget*> docks_;
    QHash<QDockWidget*, int> room_;
};
