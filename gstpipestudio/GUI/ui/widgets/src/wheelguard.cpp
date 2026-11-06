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
    auto* widget = qobject_cast<QWidget*>(watched);
    QWidget* control = widget;
    while (control && !qobject_cast<QAbstractSpinBox*>(control) && !qobject_cast<QComboBox*>(control)
           && !qobject_cast<QSlider*>(control) && !qobject_cast<QDial*>(control)) control = control->parentWidget();
    if (!control) return false;
    if (event->type() == QEvent::Enter && hovered_ != control) {
        hovered_ = control; dwell_.start();
    } else if (event->type() == QEvent::Leave && widget == hovered_) {
        hovered_.clear(); dwell_.invalidate();
    } else if (event->type() == QEvent::Wheel) {
        if (hovered_ == control && dwell_.isValid() && dwell_.elapsed() >= 500) return false;
        hovered_ = control; dwell_.start();
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
    return false;
}
