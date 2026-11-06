#pragma once
#include <QObject>
#include <QPointer>
#include <QElapsedTimer>
class QApplication;
class QWidget;
// Shared policy for value controls: scrolling edits only after a deliberate hover.
class WheelGuard final : public QObject {
public:
    static void install(QApplication& app);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    explicit WheelGuard(QObject* parent) : QObject(parent) {}
    QPointer<QWidget> hovered_;
    QElapsedTimer dwell_;
};
