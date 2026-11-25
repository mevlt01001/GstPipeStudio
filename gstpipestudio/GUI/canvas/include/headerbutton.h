// headerbutton.h – the small buttons in a template's title: "+" opens a collapsed box, "−" closes its frame. Both
// template items place and draw them with these.
#pragma once

#include <QColor>
#include <QRectF>

class QPainter;

namespace headerbutton {

enum class Glyph { Plus, Minus };

// The slot-th button from the right, its right edge `right` and its top `top`.
QRectF at(double right, double top, int slot);
void paint(QPainter* p, const QRectF& r, Glyph glyph, const QColor& colour);

}  // namespace headerbutton
