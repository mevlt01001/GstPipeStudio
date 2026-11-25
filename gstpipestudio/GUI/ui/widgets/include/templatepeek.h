// templatepeek.h – a quick look inside a template before it is inserted or expanded: a small canvas of its real
// pipeline, laid out by the same engine as the canvas (branches, several sinks, nested templates in a green frame),
// and its parameters. Shown after a 500 ms hover over a template in the Add template page and over a collapsed template
// on the canvas, once its layout is ready; any other mouse move hides it. While it is open no tooltip shows.
#pragma once

#include <QFrame>
#include <QHash>
#include <QPointer>
#include <QStringList>

#include "graph.h"
#include "layoutgraph.h"

class TemplatePeek : public QFrame {
    Q_OBJECT
public:
    static constexpr int kHoverMs = 500;

    explicit TemplatePeek(const gps::Graph& content, QWidget* parent = nullptr);
    QSize sizeHint() const override;
    bool laidOut() const { return laidOut_; }

    // One peek at a time, near the pointer; hide() closes it.
    static void show(const gps::Graph& content, const QPoint& globalPos, QWidget* parent);
    static void hide();
    static TemplatePeek* current();
    // The content as the layout engine sees it: a box per element with its linked pads as ports. Public for tests.
    static layout::Node tree(const gps::Graph& content);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;  // no tooltip covers an open peek

private:
    void place(const layout::Result& result);  // the answer: scaled to fit, then shown near where it was asked

    gps::Graph content_;
    layout::Result laid_;
    QStringList params_;
    QSizeF canvas_;
    double scale_ = 1;
    QPoint at_;
    bool laidOut_ = false;
    static QPointer<TemplatePeek> shown_;
};
