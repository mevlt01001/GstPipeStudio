// spinner.cpp
#include "spinner.h"

#include <QPainter>

#include "theme.h"

Spinner::Spinner(QWidget* parent) : QWidget(parent) {
    setObjectName("spinner");
    setFixedSize(20, 20);
    timer_.setInterval(25);
    connect(&timer_, &QTimer::timeout, this, qOverload<>(&QWidget::update));
}

void Spinner::showEvent(QShowEvent*) {
    clock_.start();
    timer_.start();
}

void Spinner::hideEvent(QHideEvent*) { timer_.stop(); }

void Spinner::paintEvent(QPaintEvent*) {
    constexpr double kWidth = 2.5;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF ring = QRectF(rect()).adjusted(kWidth, kWidth, -kWidth, -kWidth);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Theme::line(), kWidth));
    p.drawEllipse(ring);
    int turn = static_cast<int>(clock_.elapsed() % 1000);  // one turn per second, clockwise
    p.setPen(QPen(Theme::accent(), kWidth, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(ring, (90 - turn * 360 / 1000) * 16, -270 * 16);
}
