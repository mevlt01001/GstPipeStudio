// panels.cpp – see panels.h.
#include "panels.h"

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>
#include <QStackedWidget>

Panels::Panels(QMainWindow* window) : QObject(window), window_(window) {
    window->setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);  // tabs on top, as the row looked before
    window->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AllowNestedDocks);
}

void Panels::add(QWidget* panel, const QString& title, int room, Qt::DockWidgetArea side) {
    if (QDockWidget* made = titled(title)) {
        static_cast<QStackedWidget*>(made->widget())->addWidget(panel);
        return;
    }
    auto* dock = new QDockWidget(title, window_);
    dock->setObjectName(objectNameOf(title));  // what the remembered layout knows it by
    auto* pages = new QStackedWidget(dock);
    pages->addWidget(panel);
    dock->setWidget(pages);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    dock->toggleViewAction()->setObjectName("actionPanel" + QString(title).remove(' '));
    window_->addDockWidget(side, dock);
    if (QDockWidget* first = firstOn(side)) window_->tabifyDockWidget(first, dock);
    docks_ << dock;
    room_[dock] = room;
    side_[dock] = side;
}

QDockWidget* Panels::firstOn(Qt::DockWidgetArea side) const {
    for (QDockWidget* d : docks_) {
        if (side_.value(d) == side) return d;
    }
    return nullptr;
}

// By its name, not its title: a title can change (the Explorer counts unsaved tabs).
QDockWidget* Panels::titled(const QString& title) const {
    for (QDockWidget* d : docks_) {
        if (d->objectName() == objectNameOf(title)) return d;
    }
    return nullptr;
}

QString Panels::objectNameOf(QString title) { return "panel" + title.remove(' '); }

QDockWidget* Panels::dock(QWidget* panel) const {
    for (QDockWidget* d : docks_) {
        if (static_cast<QStackedWidget*>(d->widget())->indexOf(panel) >= 0) return d;
    }
    return nullptr;
}

// View > Panels keeps the panel's own name: Qt gives its toggle the new title, so it gets the old text back.
void Panels::retitle(QWidget* panel, const QString& title) {
    QDockWidget* d = dock(panel);
    if (!d || d->windowTitle() == title) return;
    const QString name = d->toggleViewAction()->text();
    d->setWindowTitle(title);
    d->toggleViewAction()->setText(name);
}

void Panels::bring(QWidget* panel) {
    if (QDockWidget* d = dock(panel)) static_cast<QStackedWidget*>(d->widget())->setCurrentWidget(panel);
}

void Panels::show(QWidget* panel) {
    QDockWidget* d = dock(panel);
    if (!d) return;
    bring(panel);
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
        window_->addDockWidget(side_.value(d), d);
        d->show();
    }
    for (QDockWidget* d : docks_) {
        QDockWidget* first = firstOn(side_.value(d));
        if (first != d) window_->tabifyDockWidget(first, d);
    }
    if (QDockWidget* bottom = firstOn(Qt::BottomDockWidgetArea)) window_->resizeDocks({bottom}, {240}, Qt::Vertical);
    if (QDockWidget* left = firstOn(Qt::LeftDockWidgetArea)) window_->resizeDocks({left}, {260}, Qt::Horizontal);
    show(front);
}
