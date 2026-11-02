// spinner.h – the circular wait indicator (UI_RULES 3.1): a 270 degree accent arc on a faint ring, painted with
// QPainter, one turn per second. Its timer runs only while the widget is visible.
#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class Spinner : public QWidget {
    Q_OBJECT
public:
    explicit Spinner(QWidget* parent = nullptr);

    bool animating() const { return timer_.isActive(); }
    QSize sizeHint() const override { return QSize(20, 20); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QTimer timer_;
    QElapsedTimer clock_;
};
