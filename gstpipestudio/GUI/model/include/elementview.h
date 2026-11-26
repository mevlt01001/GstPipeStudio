// elementview.h – what the canvas shows of one element, worked out by the Controller (SPEC 8).
// The canvas only draws these; every decision about which pads exist lives in the Controller.
#pragma once

#include <QList>
#include <QPointF>
#include <QString>

#include "canvasmetrics.h"
#include "graph.h"

struct PadView {
    enum State { Free, Linked, Unknown, Empty };  // Free/Unknown/Linked for src pads, Empty/Linked for sink pads

    gps::PadRef ref;       // which pad it is: element, identity and side
    QString label;         // the name to show (the real pad name when the last update knew it)
    QString media;         // media type of the caps, "" when unknown
    QString tip;           // full caps text for the tooltip
    QString caps;          // live negotiated caps or user filter
    State state = Free;
    int link = -1;         // index in Graph::links, -1 when free
};

struct ElementView {
    QString name, factory;
    QString displayTitle;       // shown as header title when non-empty (e.g. Template name)
    QString longName;           // the factory's long name, under the title
    QString groupId;            // the template instance it is drawn in
    bool isTemplate = false; // true when this item is a collapsed multi-element Template
    QStringList templateMembers;   // member element names when part of a group
    bool isSource = false;
    bool isSink = false;
    bool isTracked = true;
    bool nvidia = false;
    QList<PadView> sinks, srcs;
    QStringList propLines;  // "name = value", already limited to metrics::kMaxPropLines
    QString note;           // error, warning or info text under the pads
    bool isError = false;   // note is an error (red); otherwise a warning (orange) or info (blue)
    bool isInfo = false;    // untracked / unlinked info note (ℹ)

    double height() const {
        return metrics::boxHeight(qMax(sinks.size(), srcs.size()), propLines.size(), !note.isEmpty());
    }
};

struct TemplateInfo {
    QString name;         // template name, e.g. "MyDecoder"
    QString description;  // summary, e.g. "videoconvert → videoscale"
    bool isSource = false;
    bool isSink = false;
    bool hasFreeSink = false;
    bool hasFreeSrc = false;
    int elementCount = 0;
};

// What the canvas shows of an expanded template: a border around its members, its name, and the template's pads on the
// border (its free pads and the ones linked to the outside, like the ghost pads of a GstBin).
struct FrameView {
    QString id, title;
    QStringList members;  // every element inside, nested ones too
    QList<PadView> pads;
    int depth = 0;        // templates around it
};

struct LinkView {
    int index = -1;
    QString id;             // gps::LinkRef::id(): the name the layout gives its route
    QList<QList<QPointF>> route;  // pieces of the route of the last nice alignment, empty when the canvas changed since
    gps::PadRef src, sink;  // the pads at its two ends
    QString badge;          // "caps · 2": the link carries a caps choice with 2 pinned fields; "" when Auto
    QString tip;            // the choice itself
    QString negotiatedCaps; // live negotiated caps from GStreamer (or user caps filter)
};
