#include "wheelguard.h"
#include <QApplication>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QSlider>
#include <QDial>
#include <QEvent>
#include <QWheelEvent>
#include <QAbstractScrollArea>

void WheelGuard::install(QApplication& app) {
    if (app.findChild<QObject*>("valueWheelGuard")) return;
    auto* guard = new WheelGuard(&app);
    guard->setObjectName("valueWheelGuard");
    app.installEventFilter(guard);
}
bool WheelGuard::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() != QEvent::Wheel) return false;
    QWidget* control = qobject_cast<QWidget*>(watched);
    while (control && !qobject_cast<QAbstractSpinBox*>(control) && !qobject_cast<QComboBox*>(control)
           && !qobject_cast<QSlider*>(control) && !qobject_cast<QDial*>(control)) control = control->parentWidget();
    if (!control) return false;
    auto* wheel = static_cast<QWheelEvent*>(event);
    for (auto* parent = control->parentWidget(); parent; parent = parent->parentWidget()) {
        if (auto* scroll = qobject_cast<QAbstractScrollArea*>(parent)) {
            QWheelEvent forwarded(scroll->viewport()->mapFromGlobal(wheel->globalPosition().toPoint()),
                                  wheel->globalPosition(), wheel->pixelDelta(), wheel->angleDelta(),
                                  wheel->buttons(), wheel->modifiers(), wheel->phase(), wheel->inverted());
            QApplication::sendEvent(scroll->viewport(), &forwarded);
            break;
        }
    }
    wheel->accept();
    return true;
}
