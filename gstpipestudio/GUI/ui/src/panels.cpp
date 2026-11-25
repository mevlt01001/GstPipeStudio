// panels.cpp – see panels.h.
#include "panels.h"

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>

Panels::Panels(QMainWindow* window) : QObject(window), window_(window) {
    window->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);  // tabs on top, as the row looked before
    window->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);
}

void Panels::add(QWidget* panel, const QString& title, int room) {
    auto* dock = new QDockWidget(title, window_);
    QString name = title;
    dock->setObjectName("panel" + name.remove(' '));  // what the remembered layout knows it by
    dock->setWidget(panel);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    dock->toggleViewAction()->setObjectName("actionPanel" + name);
    window_->addDockWidget(Qt::BottomDockWidgetArea, dock);
    if (!docks_.isEmpty()) window_->tabifyDockWidget(docks_.front(), dock);
    docks_ << dock;
    room_[dock] = room;
}

QDockWidget* Panels::dock(QWidget* panel) const {
    for (QDockWidget* d : docks_) {
        if (d->widget() == panel) return d;
    }
    return nullptr;
}

void Panels::show(QWidget* panel) {
    QDockWidget* d = dock(panel);
    if (!d) return;
    d->show();
    d->raise();
    const int room = room_.value(d);
    const Qt::DockWidgetArea area = window_->dockWidgetArea(d);
    if (room > 0 && !d->isFloating() && (area == Qt::BottomDockWidgetArea || area == Qt::TopDockWidgetArea) && d->height() < room)
        window_->resizeDocks({d}, {room}, Qt::Vertical);
}

bool Panels::showing(QWidget* panel) const {
    QDockWidget* d = dock(panel);
    return d && d->isVisible() && panel->isVisible();
}

QList<QAction*> Panels::toggles() const {
    QList<QAction*> out;
    for (QDockWidget* d : docks_) out << d->toggleViewAction();
    return out;
}

void Panels::reset(QWidget* front) {
    for (QDockWidget* d : docks_) {
        d->setFloating(false);
        window_->addDockWidget(Qt::BottomDockWidgetArea, d);
        d->show();
    }
    for (int i = 1; i < docks_.size(); ++i) window_->tabifyDockWidget(docks_.front(), docks_[i]);
    if (!docks_.isEmpty()) window_->resizeDocks({docks_.front()}, {240}, Qt::Vertical);
    show(front);
}
