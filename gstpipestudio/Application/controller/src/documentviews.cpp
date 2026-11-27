// documentviews.cpp – see documentviews.h.
#include "documentviews.h"

#include <QRectF>
#include <algorithm>

#include "canvaslayout.h"
#include "canvasmetrics.h"
#include "capstext.h"
#include "factorycatalog.h"
#include "templateinstances.h"
#include "templates.h"

namespace {

// The chip on a link: "caps" and the number of pinned fields. The full text is the tooltip: the gap between
// two element boxes is only 40 px wide.
QString badgeOf(const QString& caps) {
    size_t pinned = gps::CapsChoice::fromText(toStd(caps)).fields.size();
    return pinned == 0 ? QString("caps") : QString("caps · %1").arg(pinned);
}

// At most metrics::kMaxPropLines lines; the last says how many more there are.
QStringList limitedLines(const QStringList& lines) {
    if (lines.size() <= metrics::kMaxPropLines) return lines;
    QStringList shown = lines.mid(0, metrics::kMaxPropLines - 1);
    return shown << QString("… +%1 more").arg(lines.size() - shown.size());
}

}  // namespace

DocumentViews::DocumentViews(const gps::Pipeline& pipeline, const FactoryCatalog& catalog, const TemplateInstances& templates,
                             const CanvasLayout& layout)
    : pipeline_(pipeline), catalog_(catalog), templates_(templates), layout_(layout) {}

// A draft is never kept: it changes under the edit that holds it.
std::uint64_t DocumentViews::revision() const { return pipeline_.editor().editing() ? kNeverKept : pipeline_.lastChange().revision; }

gps::Names DocumentViews::names() const {
    const std::uint64_t now = revision();
    if (now == kNeverKept || now != namesRevision_) {
        names_ = gps::Names(plan());
        namesRevision_ = now;
    }
    return names_;
}

QString DocumentViews::padName(const gps::PadRef& pad) const { return fromStd(names().pad(plan(), pad)); }

Outline DocumentViews::outline() const {
    const std::uint64_t now = revision();
    const QSet<QString> expanded = templates_.expandedSet();
    if (now == kNeverKept || now != outlineRevision_ || expanded != outlineExpanded_) {
        outline_ = Outline(plan(), expanded);
        outlineRevision_ = now;
        outlineExpanded_ = expanded;
    }
    return outline_;
}

QString DocumentViews::elementNote(const QString& element, bool* isError) const {
    *isError = false;
    const UpdateResult& r = pipeline_.observations();
    if (r.valid() && !r.ok() && fromStd(r.errorElement) == element) {
        *isError = true;
        return fromStd(r.message);
    }
    for (const std::vector<std::string>* list : {&r.refused, &r.missing}) {
        for (const std::string& text : *list) {
            const QString entry = fromStd(text);
            if (entry.startsWith(element + ".") || entry.startsWith(element + ":")) return entry;
        }
    }
    return QString();
}

ElementView DocumentViews::view(const QString& name) const {
    ElementView v;
    v.name = name;
    const gps::Element* element = plan().find(toStd(name));
    if (!element) return v;

    v.factory = fromStd(element->factory);
    v.displayTitle = fromStd(names().element(element->name));
    v.groupId = fromStd(element->group);
    v.groupTitle = fromStd(names().instance(element->group));
    if (!v.groupId.isEmpty()) v.templateMembers = outline().membersOf(v.groupId);
    if (const FactoryEntry* e = catalog_.entry(v.factory)) {
        v.nvidia = e->nvidia;
        v.longName = fromStd(e->longName);
    }

    // Changed properties, the exposed ones marked.
    QStringList lines;
    for (const auto& entry : element->props) {
        const auto& key = entry.first;
        const auto& value = entry.second;
        bool exposed = std::any_of(plan().exposed.begin(), plan().exposed.end(),
                                   [&](const gps::ExposedParam& p) { return p.element == element->name && p.property == key; });
        lines << (exposed ? "↗ " : "") + fromStd(key) + " = " + fromStd(value);
    }
    v.propLines = limitedLines(lines);
    v.note = elementNote(name, &v.isError);

    const ElementInfo* inf = catalog_.info(v.factory);
    if (!inf) return v;
    const gps::Role role = shape().role(gps::NodeRef::element(element->name));
    v.isSource = role == gps::Role::Source;
    v.isSink = role == gps::Role::Sink;
    v.isTracked = plan().isElementTracked(element->name, catalog_.traits());
    if (!v.isTracked && v.note.isEmpty()) {
        v.isInfo = true;
        v.note = "ℹ Unlinked sink pad — not tracked until fed by a source.";
    }
    // Pads: the Pipeline owns which exist (Pipeline::padsOf); the view adds what the canvas shows of them.
    for (const gps::Pad& pad : element->pads) (pad.direction == gps::Pad::Direction::Source ? v.srcs : v.sinks) << padView(*element, pad, v.isTracked);
    return v;
}

PadView DocumentViews::padView(const gps::Element& element, const gps::Pad& pad, bool tracked) const {
    const bool src = pad.direction == gps::Pad::Direction::Source;
    const ElementInfo* inf = catalog_.info(fromStd(element.factory));
    const PadTemplateInfo* t = inf ? inf->padTemplate(pad.templateName, src ? "src" : "sink") : nullptr;
    PadView p;
    p.ref = {element.name, pad.identity, pad.direction};
    p.label = fromStd(gps::Names::label(pad));
    if (src) {
        p.link = plan().linkFrom(element.name, pad.identity);
        const std::string& caps = pad.negotiatedCaps.empty() ? pad.allowedCaps : pad.negotiatedCaps;
        p.media = fromStd(gps::mediaOf(caps));
        p.tip = fromStd(caps);
        p.caps = fromStd(pad.negotiatedCaps);
        p.state = p.link >= 0 ? PadView::Linked : (!tracked || pad.unknownData) ? PadView::Unknown : PadView::Free;
    } else {
        p.link = plan().linkInto(element.name, pad.identity);
        p.media = t && !t->structures.empty() ? fromStd(gps::structureTitle(t->structures.front())) : QString();
        p.tip = fromStd(pad.templateCaps);
        if (!pad.negotiatedCaps.empty()) {
            p.media = fromStd(gps::mediaOf(pad.negotiatedCaps));
            p.tip = p.caps = fromStd(pad.negotiatedCaps);
        }
        p.state = p.link >= 0 ? PadView::Linked : PadView::Empty;
    }
    return p;
}

// What the canvas draws of the pads a group shows: each one as its element's box would draw it.
QList<PadView> DocumentViews::padViews(const std::vector<gps::Port>& ports) const {
    QList<PadView> out;
    for (const gps::Port& port : ports) {
        const gps::Element* e = plan().find(port.pad.element);
        if (!e) continue;
        for (const gps::Pad& pad : e->pads) {
            if (pad.identity == port.pad.identity && pad.direction == port.pad.direction)
                out << padView(*e, pad, plan().isElementTracked(e->name, catalog_.traits()));
        }
    }
    return out;
}

ElementView DocumentViews::templateView(const QString& groupId) const {
    ElementView v;
    const QStringList members = outline().membersOf(groupId);
    if (members.isEmpty()) return v;

    v.name = members.first();
    v.displayTitle = fromStd(names().instance(toStd(groupId)));
    v.groupId = groupId;
    v.isTemplate = true;
    v.templateMembers = members;
    v.isTracked = false;

    QStringList factories;
    QStringList propSummary;
    for (const QString& m : members) {
        ElementView mv = view(m);
        factories << mv.factory;
        if (mv.nvidia) v.nvidia = true;
        if (mv.isTracked) v.isTracked = true;
        if (mv.isError && !mv.note.isEmpty()) {
            v.isError = true;
            v.note = mv.displayTitle + ": " + mv.note;
        } else if (v.note.isEmpty() && !mv.note.isEmpty() && !mv.isInfo) {
            v.note = mv.displayTitle + ": " + mv.note;
        }
        for (const QString& pl : mv.propLines) {
            if (!pl.startsWith("…")) propSummary << (mv.displayTitle + "." + pl);
        }
    }
    const gps::NodePorts ports = shape().ports(gps::NodeRef::instance(toStd(groupId)));
    v.sinks = padViews(ports.sinks);
    v.srcs = padViews(ports.srcs);
    const gps::Role role = gps::NodeShape::role(ports);
    v.isSource = role == gps::Role::Source;
    v.isSink = role == gps::Role::Sink;
    v.factory = QString("template (%1) · %2").arg(members.size()).arg(factories.join(" → "));
    v.propLines = limitedLines(propSummary);
    if (!v.isTracked && v.note.isEmpty() && !v.isSource) {
        v.isInfo = true;
        v.note = "ℹ Unlinked template — not tracked until fed by a source.";
    }
    return v;
}

ElementView DocumentViews::boxView(const QString& box) const {
    return plan().find(toStd(box)) ? view(box) : templateView(box);
}

FrameView DocumentViews::frameView(const QString& instanceId) const {
    FrameView v;
    const gps::TemplateInstance* t = plan().instance(toStd(instanceId));
    if (!t) return v;
    v.id = instanceId;
    v.title = fromStd(names().instance(t->id));
    v.members = outline().membersOf(instanceId);
    v.depth = static_cast<int>(gps::templates::chain(plan(), t->id).size());
    const Outline o = outline();
    for (const QString& frame : o.visibleFrames()) {
        if (frame != instanceId && o.framesAround(o.membersOf(frame).value(0)).contains(instanceId)) v.inner << frame;
    }
    const gps::NodePorts ports = shape().ports(gps::NodeRef::instance(t->id));
    v.pads = padViews(ports.sinks) + padViews(ports.srcs);
    return v;
}

QList<LinkView> DocumentViews::linkViews() const {
    QList<LinkView> out;
    for (size_t i = 0; i < plan().links.size(); ++i) {
        const gps::Connection& l = plan().links[i];
        LinkView v;
        v.index = static_cast<int>(i);
        v.src = plan().srcOf(i);
        v.sink = plan().sinkOf(i);
        v.id = fromStd(gps::LinkRef{v.src}.id());
        v.route = layout_.route(v.id);
        v.badge = l.caps.empty() ? QString() : badgeOf(fromStd(l.caps));
        v.tip = fromStd(l.caps);
        const PadReport* srcRep = pipeline_.observations().pad(l.srcElement, l.srcPad, true);
        if (srcRep && !srcRep->caps.empty()) {
            v.negotiatedCaps = fromStd(srcRep->caps);
        } else if (!l.caps.empty()) {
            v.negotiatedCaps = fromStd(l.caps);
        }
        if (v.tip.isEmpty() && !v.negotiatedCaps.isEmpty()) v.tip = v.negotiatedCaps;
        out << v;
    }
    return out;
}

std::vector<StructureInfo> DocumentViews::padStructures(const QString& element, const QString& identity) const {
    const PadReport* r = pipeline_.observations().pad(toStd(element), toStd(identity), true);
    return r ? r->allowedStructures : std::vector<StructureInfo>();
}

QString DocumentViews::padAllowed(const QString& element, const QString& identity) const {
    const PadReport* r = pipeline_.observations().pad(toStd(element), toStd(identity), true);
    return r ? fromStd(r->allowed) : QString();
}

const PadTemplateInfo* DocumentViews::linkSinkTemplate(int link) const {
    if (link < 0 || link >= static_cast<int>(plan().links.size())) return nullptr;
    const gps::Connection& l = plan().links[link];
    const gps::Element* sink = plan().find(l.sinkElement);
    const ElementInfo* inf = sink ? catalog_.info(fromStd(sink->factory)) : nullptr;
    return inf ? inf->padTemplate(l.sinkPad, "sink") : nullptr;
}

QList<ExistingTarget> DocumentViews::existingTargets(const QString& element, const QString& identity) const {
    QList<ExistingTarget> out;
    std::vector<StructureInfo> allowed = padStructures(element, identity);
    for (const gps::Element& e : plan().elements) {
        const ElementInfo* inf = catalog_.info(fromStd(e.factory));
        if (!inf || plan().wouldCycle(toStd(element), e.name)) continue;
        for (const PadTemplateInfo& t : inf->sinkTemplates()) {
            if (!gps::structuresMeet(allowed, t.structures)) continue;
            if (!gps::isTemplateName(t.name) && plan().sinkPadTaken(e.name, t.name)) continue;
            out.append({fromStd(e.name), fromStd(e.factory), fromStd(t.name), t.isRequest()});
        }
    }
    return out;
}

std::vector<PadTemplateInfo> DocumentViews::sinkChoices(const ElementInfo& inf, const QString& element, const QString& identity) const {
    return inf.sinkTemplatesFor(padStructures(element, identity));
}

QPointF DocumentViews::freeSpot() const {
    double y = 40;
    for (const gps::Element& e : plan().elements) y = std::max(y, e.y + view(fromStd(e.name)).height() + 40);
    return QPointF(40, y);
}
