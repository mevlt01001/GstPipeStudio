// outline.h – what the canvas draws as one box, worked out once for a plan and the set of expanded template copies: an
// element on its own, or a collapsed copy holding all its members; and the frames of the expanded copies around them.
// Controller, CanvasLayout and CanvasScene ask it instead of walking the template chain each.
#pragma once

#include <QHash>
#include <QSet>
#include <QStringList>

#include "graph.h"

class Outline {
public:
    Outline() = default;
    Outline(const gps::Graph& plan, const QSet<QString>& expanded);

    QString boxOf(const QString& element) const;              // itself, or the outermost collapsed copy around it
    QString collapsedTemplate(const QString& element) const;  // that copy; "" when the element is a box of its own
    QString anchorOf(const QString& box) const;               // the element whose position the box is drawn at
    QStringList membersOf(const QString& box) const;          // an element: itself; a copy: every element inside it
    QStringList boxesOf(const QStringList& elements) const;   // each box once, in the order of the elements
    QStringList framesAround(const QString& element) const;   // the expanded copies around it, outermost first
    QStringList expand(const QStringList& elements) const;    // the elements, each collapsed copy among them whole
    QStringList visibleFrames() const { return visible_; }    // expanded copies whose every enclosing copy is expanded

private:
    QHash<QString, QString> box_;          // element -> its box
    QHash<QString, QStringList> frames_;   // element -> expanded copies around it, outermost first
    QHash<QString, QStringList> members_;  // copy -> its members, nested ones too
    QStringList visible_;
};
