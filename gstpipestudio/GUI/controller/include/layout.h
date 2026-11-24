// layout.h – where element boxes go on the canvas. Pure geometry: the graph's levels, the boxes and the link labels in,
// positions out. The Controller applies them as one undoable move.
#pragma once

#include <QList>
#include <QPair>
#include <QPointF>
#include <QString>
#include <map>
#include <string>

namespace layout {

struct Box {
    QString name;
    QPointF at;
    double height = 0;
};
using Placements = QList<QPair<QString, QPointF>>;

// A row (Qt::Horizontal) or a column (Qt::Vertical): the boxes share the mean of the other coordinate, keep their order,
// and move apart where they would overlap.
Placements align(QList<Box> boxes, Qt::Orientation orientation);

// Columns by level (Graph::levels()), each column in the boxes' old top-to-bottom order. A column is followed by room
// for the widest caps label leaving it: labels holds (source level, caps text) per link.
Placements arrange(const std::map<std::string, int>& levels, QList<Box> boxes, const QList<QPair<int, QString>>& labels);

}  // namespace layout
