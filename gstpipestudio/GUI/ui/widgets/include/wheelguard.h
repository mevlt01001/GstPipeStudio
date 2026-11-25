#pragma once
#include <QObject>
class QApplication;
// Shared policy for value controls: the mouse wheel never edits a value, it scrolls the form around the control.
// Values are typed or chosen.
class WheelGuard final : public QObject {
public:
    static void install(QApplication& app);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    explicit WheelGuard(QObject* parent) : QObject(parent) {}
};
