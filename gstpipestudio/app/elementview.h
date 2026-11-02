// elementview.h – what the canvas shows of one element, worked out by the Controller (SPEC 8).
// The canvas only draws these; every decision about which pads exist lives in the Controller.
#pragma once

#include <QList>
#include <QString>

#include "canvasmetrics.h"

struct PadView {
    enum State { Free, Linked, Unknown, Empty };  // Free/Unknown/Linked for src pads, Empty/Linked for sink pads

    QString element;
    QString identity;      // src: pad name or "<template>#<k>"; sink: pad name or "<template>#<k>" for Request
    QString label;         // the name to show (the real pad name when the last update knew it)
    QString templateName;
    QString media;         // media type of the caps, "" when unknown
    QString tip;           // full caps text for the tooltip
    bool isSrc = false;
    State state = Free;
    int link = -1;         // index in Plan::links, -1 when free
};

struct ElementView {
    QString name, factory;
    bool isSource = false;
    bool nvidia = false;
    QList<PadView> sinks, srcs;
    QStringList propLines;  // "name = value", already limited to metrics::kMaxPropLines
    QString note;           // error or warning text under the pads
    bool isError = false;   // note is an error (red); otherwise a warning (orange)

    double height() const {
        return metrics::boxHeight(qMax(sinks.size(), srcs.size()), propLines.size(), !note.isEmpty());
    }
};

struct LinkView {
    int index = -1;
    QString srcElement, srcIdentity, sinkElement, sinkIdentity;
    QString badge;  // "caps · 2": the link carries a caps choice with 2 pinned fields; "" when Otomatik
    QString tip;    // the choice itself
};
