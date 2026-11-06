#include "connectionpreview.h"
#include "theme.h"
#include <QPainter>
ConnectionPreview::ConnectionPreview() {
    setAcceptedMouseButtons(Qt::NoButton);
    setZValue(4);
    animation_.setDuration(210);
    animation_.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        end_ = value.toPointF();
        rebuild();
    });
    connect(&animation_, &QVariantAnimation::finished, this, &QObject::deleteLater);
}
void ConnectionPreview::setEndpoints(const QPointF& start, const QPointF& end, bool source) {
    start_ = start;
    end_ = end;
    source_ = source;
    rebuild();
}
void ConnectionPreview::rebuild() {
    prepareGeometryChange();
    const qreal reach = qBound(20.0, QLineF(start_, end_).length() * 0.35, 160.0);
    const qreal direction = source_ ? 1.0 : -1.0;
    path_ = QPainterPath(start_);
    path_.cubicTo(start_ + QPointF(direction * reach, 0), end_ - QPointF(direction * reach, 0), end_);
    update();
}
void ConnectionPreview::retract() {
    animation_.setStartValue(end_);
    animation_.setEndValue(start_);
    animation_.start();
}
void ConnectionPreview::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    QColor halo = Theme::accent();
    halo.setAlpha(40);
    painter->setPen(QPen(halo, 7, Qt::SolidLine, Qt::RoundCap));
    painter->drawPath(path_);
    painter->setPen(QPen(Theme::accentHover(), 2.3, Qt::SolidLine, Qt::RoundCap));
    painter->drawPath(path_);
}
