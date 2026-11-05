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
    QString caps;          // live negotiated caps or user filter
    bool isSrc = false;
    State state = Free;
    int link = -1;         // index in Plan::links, -1 when free
};

struct ElementView {
    QString name, factory;
    QString displayTitle;       // shown as header title when non-empty (e.g. Custom Element name)
    QString groupId;            // "<CustomTitle>#<id>" when part of a custom element/group
    bool isCustomGroup = false; // true when this item is a collapsed multi-element Custom Element
    QStringList groupMembers;   // member element names when part of a group
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

struct CustomElementInfo {
    QString name;         // custom element name, e.g. "MyDecoder"
    QString description;  // summary, e.g. "videoconvert → videoscale"
    bool isSource = false;
    bool isSink = false;
    bool hasFreeSink = false;
    bool hasFreeSrc = false;
    int elementCount = 0;
};

struct LinkView {
    int index = -1;
    QString srcElement, srcIdentity, sinkElement, sinkIdentity;
    QString badge;          // "caps · 2": the link carries a caps choice with 2 pinned fields; "" when Auto
    QString tip;            // the choice itself
    QString negotiatedCaps; // live negotiated caps from GStreamer (or user caps filter)
};
