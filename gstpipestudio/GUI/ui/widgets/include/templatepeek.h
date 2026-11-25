// templatepeek.h – a quick look inside a template before it is inserted or expanded: its elements as small boxes in
// columns (by the links), its links, and its parameters. Shown after a 500 ms hover over a template in the Add template
// page and over a collapsed template on the canvas; any other mouse move hides it.
#pragma once

#include <QFrame>
#include <QHash>
#include <QPointer>
#include <QStringList>

#include "graph.h"

class TemplatePeek : public QFrame {
    Q_OBJECT
public:
    static constexpr int kHoverMs = 500;

    explicit TemplatePeek(const gps::Graph& content, QWidget* parent = nullptr);
    QSize sizeHint() const override;

    // One peek at a time, near the pointer; hide() closes it.
    static void show(const gps::Graph& content, const QPoint& globalPos, QWidget* parent);
    static void hide();
    static TemplatePeek* current();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    gps::Graph content_;
    QHash<QString, QRectF> boxes_;
    QStringList params_;
    QSizeF canvas_;
    static QPointer<TemplatePeek> shown_;
};
