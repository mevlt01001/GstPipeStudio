// headerbutton.cpp – see headerbutton.h.
#include "headerbutton.h"

#include <QPainter>

#include "theme.h"

namespace headerbutton {

namespace {
constexpr double kWidth = 24, kHeight = 20, kSpace = 4;
}

QRectF at(double right, double top, int slot) {
    return QRectF(right - kWidth - slot * (kWidth + kSpace), top, kWidth, kHeight);
}

void paint(QPainter* p, const QRectF& r, Glyph glyph, const QColor& colour) {
    p->save();
    p->setPen(QPen(colour, 1.5, Qt::SolidLine, Qt::RoundCap));
    p->setBrush(Theme::background());
    p->drawRoundedRect(r, 4, 4);
    const QPointF c = r.center();
    p->drawLine(QPointF(r.left() + 7, c.y()), QPointF(r.right() - 7, c.y()));
    if (glyph == Glyph::Plus) p->drawLine(QPointF(c.x(), r.top() + 5), QPointF(c.x(), r.bottom() - 5));
    p->restore();
}

}  // namespace headerbutton
