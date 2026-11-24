// layout.cpp – see layout.h.
#include "layout.h"

#include <QFontMetricsF>
#include <algorithm>

#include "canvasmetrics.h"
#include "capslabel.h"

namespace layout {

Placements align(QList<Box> boxes, Qt::Orientation orientation) {
    const bool row = orientation == Qt::Horizontal;
    auto along = [row](const Box& b) { return row ? b.at.x() : b.at.y(); };
    auto across = [row](const Box& b) { return row ? b.at.y() : b.at.x(); };
    double sum = 0;
    for (const Box& b : boxes) sum += across(b);
    const double line = std::round(sum / boxes.size());
    std::sort(boxes.begin(), boxes.end(), [&](const Box& a, const Box& b) { return along(a) < along(b); });

    Placements out;
    double next = boxes.isEmpty() ? 0 : along(boxes.front());  // the first position free of the previous box
    for (const Box& b : boxes) {
        const double at = std::max(along(b), next);
        out.append({b.name, row ? QPointF(at, line) : QPointF(line, at)});
        next = at + (row ? metrics::kColumn : b.height + metrics::kGap);
    }
    return out;
}

Placements arrange(const std::map<std::string, int>& levels, QList<Box> boxes, const QList<QPair<int, QString>>& labels) {
    std::sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) {
        return a.at.y() == b.at.y() ? a.name < b.name : a.at.y() < b.at.y();
    });
    std::map<int, QList<Box>> columns;
    for (const Box& b : boxes) columns[levels.at(b.name.toStdString())].append(b);

    const QFontMetricsF text(capslabel::font());
    std::map<int, double> room;
    for (const auto& [level, caps] : labels) {
        const double width = text.horizontalAdvance(capslabel::singleLine(caps));
        room[level] = std::max(room[level], std::min(480.0, width + 90.0));
    }
    Placements out;
    double x = 80;
    for (const auto& [level, column] : columns) {
        double y = 80;
        for (const Box& b : column) {
            out.append({b.name, QPointF(x, y)});
            y += b.height + 110;
        }
        x += metrics::kBoxWidth + std::max(260.0, room[level]);
    }
    return out;
}

}  // namespace layout
