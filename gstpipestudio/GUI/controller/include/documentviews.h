// documentviews.h – what the canvas and the drawer show of one document, read from its plan, its last validation and the
// catalog: the boxes (an element, a collapsed template copy), the frames of expanded copies, the links, the places a
// free src pad can go. Names and the Outline depend on the plan only: they are kept per revision (and expanded set);
// the views also follow the validation and the catalog, so they are worked out when asked.
#pragma once

#include <QList>
#include <QPointF>
#include <QSet>
#include <QString>
#include <cstdint>
#include <limits>
#include <vector>

#include "contract.h"
#include "elementview.h"
#include "names.h"
#include "nodes.h"
#include "outline.h"
#include "pipeline.h"

class CanvasLayout;
class FactoryCatalog;
class TemplateInstances;

// A place a free src pad can be linked to inside the plan.
struct ExistingTarget {
    QString element, factory, sinkPad;
    bool request = false;  // true: a new pad of a Request sink template ("Request a pad")
};

class DocumentViews {
public:
    DocumentViews(const gps::Pipeline& pipeline, const FactoryCatalog& catalog, const TemplateInstances& templates,
                  const CanvasLayout& layout);

    gps::Names names() const;    // every name the user reads; kept per revision
    Outline outline() const;     // what is drawn as one box; kept per revision and expanded set
    gps::NodeShape shape() const { return gps::NodeShape(plan(), pipeline_); }  // nodes and their pads on the plan as it is
    QString padName(const gps::PadRef& pad) const;  // names().pad() of the plan as text: "<element>.<pad>"

    ElementView view(const QString& element) const;
    ElementView templateView(const QString& instanceId) const;
    ElementView boxView(const QString& box) const;  // what is drawn for a box (Outline): an element's view or a copy's
    FrameView frameView(const QString& instanceId) const;
    QList<LinkView> linkViews() const;
    QPointF freeSpot() const;  // where a new box goes: below every other one

    std::vector<StructureInfo> padStructures(const QString& element, const QString& identity) const;  // allowed by a src pad
    QString padAllowed(const QString& element, const QString& identity) const;
    QList<ExistingTarget> existingTargets(const QString& element, const QString& identity) const;
    std::vector<PadTemplateInfo> sinkChoices(const ElementInfo& info, const QString& element, const QString& identity) const;
    const PadTemplateInfo* linkSinkTemplate(int link) const;  // template of the sink pad a link ends on

private:
    const gps::Graph& plan() const { return pipeline_.editor().view(); }
    std::uint64_t revision() const;  // what is worked out now is kept for this revision; kNeverKept for a draft
    QString elementNote(const QString& element, bool* isError) const;
    PadView padView(const gps::Element& element, const gps::Pad& pad, bool tracked) const;
    QList<PadView> padViews(const std::vector<gps::Port>& ports) const;

    const gps::Pipeline& pipeline_;
    const FactoryCatalog& catalog_;
    const TemplateInstances& templates_;
    const CanvasLayout& layout_;
    static constexpr std::uint64_t kNeverKept = std::numeric_limits<std::uint64_t>::max();
    mutable gps::Names names_;
    mutable std::uint64_t namesRevision_ = kNeverKept;
    mutable Outline outline_;
    mutable std::uint64_t outlineRevision_ = kNeverKept;
    mutable QSet<QString> outlineExpanded_;
};
