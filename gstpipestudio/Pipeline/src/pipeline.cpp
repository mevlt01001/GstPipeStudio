#include "pipeline.h"
#include "templates.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace gps {
namespace {
std::string executionKey(Graph graph) {
    graph.name.clear(); graph.complete = false; graph.nextId = 0;
    for (auto& e : graph.elements) { e.x = e.y = 0; e.group.clear(); }
    graph.instances.clear();  // template instances are presentation: the engine never reads them
    return graph.toConf();
}
bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}
}

Pipeline::Pipeline(std::string name) : editor_(*this) { graph_.name = std::move(name); clearObservations(); }
bool Pipeline::validate(const Graph& graph, std::string* error) {
    std::set<std::string> names;
    for (const auto& e : graph.elements) {
        if (e.name.empty() || e.factory.empty() || !names.insert(e.name).second)
            return fail(error, "Elements need unique non-empty identities and factories");
        if (!std::isfinite(e.x) || !std::isfinite(e.y)) return fail(error, "Element coordinates must be finite");
        for (const auto* requests : {&e.requestSrc, &e.requestSink})
            for (const auto& entry : *requests)
                if (entry.second < 0) return fail(error, "A request pad count cannot be negative");
    }
    std::set<std::pair<std::string, std::string>> sources, sinks;
    for (const auto& l : graph.links) {
        if (!names.count(l.srcElement) || !names.count(l.sinkElement) || l.srcPad.empty() || l.sinkPad.empty())
            return fail(error, "A connection refers to a missing element or empty pad identity");
        if (!sources.emplace(l.srcElement, l.srcPad).second)
            return fail(error, "A source pad already has a connection");
        if (!isTemplateName(l.sinkPad) && !sinks.emplace(l.sinkElement, l.sinkPad).second)
            return fail(error, "A sink pad already has a connection");
        if (graph.wouldCycle(l.srcElement, l.sinkElement)) return fail(error, "A connection would create a cycle");
    }
    std::set<std::string> exposed;
    for (const auto& p : graph.exposed)
        if (!names.count(p.element) || p.name.empty() || !exposed.insert(p.name).second)
            return fail(error, "An exposed property has a missing owner or duplicate name");
    std::set<std::string> instances;
    for (const auto& t : graph.instances)
        if (t.id.empty() || !instances.insert(t.id).second) return fail(error, "Template instances need unique identities");
    for (const auto& t : graph.instances) {
        if (!t.parent.empty() && !instances.count(t.parent)) return fail(error, "A template instance names a missing parent");
        if (templates::inside(graph, t.id, t.id)) return fail(error, "A template instance cannot be inside itself");
        std::set<std::string> params;
        for (const auto& p : t.params)
            if (!templates::inside(graph, p.element, t.id) || !params.insert(p.name).second)
                return fail(error, "A template parameter has a missing owner or duplicate name");
    }
    for (const auto& e : graph.elements)
        if (!e.group.empty() && !instances.count(e.group)) return fail(error, "An element names a missing template instance");
    return true;
}
void Pipeline::install(Graph graph) {
    const bool affectsExecution = executionKey(graph) != executionKey(graph_);
    nextIdentity_ = std::max(nextIdentity_, graph.nextId);
    graph.nextId = nextIdentity_; // deletion/undo cannot recycle an identity
    graph_ = std::move(graph);
    ++change_.revision;
    change_.kind = affectsExecution ? ChangeKind::Execution : ChangeKind::Presentation;
    if (affectsExecution) {
        ++change_.executionRevision;
        clearObservations();
    }
    refreshPads();
}
bool Pipeline::reset(Graph graph, std::string* error) {
    if (!validate(graph, error)) return false;
    editor_.rollback(); editor_.undo_.clear(); editor_.redo_.clear();
    install(std::move(graph));
    // A new document invalidates every run even if its serialized contents match.
    ++change_.executionRevision;
    clearObservations();
    refreshPads();
    return true;
}
ExecutionPlan Pipeline::executionPlan(const TraitsFn& traits, std::uint64_t run) const {
    return {graph_.trackedSubplan(traits), {change_.executionRevision, run}};
}
bool Pipeline::apply(const ExecutionResult& result, ExecutionTicket expected) {
    if (!(result.ticket == expected) || result.ticket.revision != change_.executionRevision) return false;
    observations_ = result.report;
    // Runtime reports cannot add authored elements, edit connections, or emit edits.
    observations_.pads.erase(std::remove_if(observations_.pads.begin(), observations_.pads.end(),
        [this](const PadReport& p) { return !graph_.find(p.element); }), observations_.pads.end());
    refreshPads();
    return true;
}
void Pipeline::clearObservations() {
    observations_ = {};
    observations_.status.clear();
}
void Pipeline::observeIdle() {
    clearObservations();
    if (!graph_.empty()) observations_.status = "ok";  // no pad report: complete() stays false
    refreshPads();
}
void Pipeline::registerFactory(std::string name, FactoryDefinition definition) {
    factories_[std::move(name)] = std::move(definition);
    refreshPads();
}
void Pipeline::refreshPads() {
    for (auto& element : graph_.elements) element.pads = padsOf(element);
}
// The one place pads exist (SPEC 4): every template gives its pads, the last run tells their native names and caps.
//   sink, not Request   one pad named after the template
//   sink, Request       "<template>#<k>" for every link into it, at least the requested count
//   src, Always         one pad named after the template
//   src, Request        "<template>#<k>" up to the requested count, or further when a link or the last run used k
//   src, Sometimes      "<template>#<k>" for every k a link or the last run used
std::vector<Pad> Pipeline::padsOf(const Element& element) const {
    std::vector<Pad> pads;
    auto factory = factories_.find(element.factory);
    if (factory == factories_.end()) {  // no template is known (engine, tools): the last run's reports are all there is
        for (const PadReport& r : observations_.pads) {
            if (r.element != element.name) continue;
            Pad pad;
            pad.identity = r.identity;
            pad.templateName = r.templateName;
            pad.direction = r.isSrc ? Pad::Direction::Source : Pad::Direction::Sink;
            pad.presence = presenceOf(r.presence);
            observe(pad, r);
            pads.push_back(std::move(pad));
        }
        return pads;
    }
    auto requested = [](const std::map<std::string, int>& counts, const std::string& name) {
        auto found = counts.find(name);
        return found == counts.end() ? 0 : found->second;
    };
    for (const Pad& t : factory->second.templates) {
        const bool src = t.direction == Pad::Direction::Source;
        std::vector<int> ordinals;  // -1: the pad named after the template
        if (src ? t.presence == Pad::Presence::Always : t.presence != Pad::Presence::Request) {
            ordinals = {-1};
        } else if (!src) {
            int n = requested(element.requestSink, t.templateName);
            n = std::max(n, static_cast<int>(std::count_if(graph_.links.begin(), graph_.links.end(), [&](const Connection& l) {
                return l.sinkElement == element.name && l.sinkPad == t.templateName;
            })));
            for (int k = 0; k < n; ++k) ordinals.push_back(k);
        } else {
            std::set<int> used;
            auto note = [&](const std::string& identity) {
                std::string templateName;
                int k = 0;
                if (splitPadIdentity(identity, &templateName, &k) && templateName == t.templateName) used.insert(k);
            };
            for (const PadReport& r : observations_.pads)
                if (r.element == element.name && r.isSrc) note(r.identity);
            for (const Connection& l : graph_.links)
                if (l.srcElement == element.name) note(l.srcPad);
            if (t.presence == Pad::Presence::Request) {
                int n = std::max(requested(element.requestSrc, t.templateName), used.empty() ? 0 : *used.rbegin() + 1);
                for (int k = 0; k < n; ++k) ordinals.push_back(k);
            } else {
                ordinals.assign(used.begin(), used.end());
            }
        }
        for (int k : ordinals) {
            Pad pad = t;
            pad.identity = k < 0 ? t.templateName : padIdentity(t.templateName, k);
            if (const PadReport* r = report(element.name, pad, std::max(k, 0))) observe(pad, *r);
            pads.push_back(std::move(pad));
        }
    }
    return pads;
}
void Pipeline::observe(Pad& pad, const PadReport& report) {
    pad.nativeName = report.pad;
    pad.negotiatedCaps = report.caps;
    pad.allowedCaps = report.allowed;
    pad.unknownData = report.unknownData;
}
// A src pad's report carries its identity; sink reports of one template come in link order, so the k-th is pad k.
const PadReport* Pipeline::report(const std::string& element, const Pad& pad, int k) const {
    if (pad.direction == Pad::Direction::Source) return observations_.pad(element, pad.identity, true);
    for (const PadReport& r : observations_.pads) {
        if (r.element == element && !r.isSrc && r.templateName == pad.templateName && k-- == 0) return &r;
    }
    return nullptr;
}
std::optional<PadAddress> Pipeline::peer(const PadAddress& address) const {
    if (address.direction == Pad::Direction::Source) {
        int i = graph_.linkFrom(address.element, address.pad);
        if (i >= 0) return PadAddress{graph_.links[i].sinkElement, graph_.sinkIdentity(i), Pad::Direction::Sink};
    } else {
        int i = graph_.linkInto(address.element, address.pad);
        if (i >= 0) return PadAddress{graph_.links[i].srcElement, graph_.links[i].srcPad, Pad::Direction::Source};
    }
    return std::nullopt;
}

Graph& GraphEditor::begin() {
    if (draft_) throw std::logic_error("Nested graph edit");
    draft_ = document_.graph_;
    return *draft_;
}
Graph& GraphEditor::draft() {
    if (!draft_) throw std::logic_error("Graph mutation requires an edit transaction");
    return *draft_;
}
const Graph& GraphEditor::view() const { return draft_ ? *draft_ : document_.graph_; }
void GraphEditor::rollback() { draft_.reset(); }
bool GraphEditor::commit(std::string* error) {
    if (!draft_) return false;
    if (!Pipeline::validate(*draft_, error)) { rollback(); return false; }
    if (draft_->toConf() == document_.graph_.toConf()) { rollback(); return false; }
    Graph next = std::move(*draft_); rollback();
    undo_.push_back({document_.graph_, next});
    if (undo_.size() > 64) undo_.erase(undo_.begin());
    redo_.clear();
    document_.install(std::move(next));
    return true;
}
bool GraphEditor::execute(const IAction& action, std::string* error) {
    const auto reason = action.unavailableReason(document_.graph_);
    if (!reason.empty()) return fail(error, reason);
    begin();
    try {
        if (!action.apply(draft(), error)) { rollback(); return false; }
        return commit(error);
    } catch (...) { rollback(); throw; }
}
bool GraphEditor::undo() {
    if (!canUndo()) return false;
    auto entry = std::move(undo_.back()); undo_.pop_back();
    document_.install(entry.before); redo_.push_back(std::move(entry)); return true;
}
bool GraphEditor::redo() {
    if (!canRedo()) return false;
    auto entry = std::move(redo_.back()); redo_.pop_back();
    document_.install(entry.after); undo_.push_back(std::move(entry)); return true;
}
Graph GraphFragment::copy(const Graph& graph) const {
    Graph out;
    std::set<std::string> selected(elements_.begin(), elements_.end());
    for (const auto& e : graph.elements) if (selected.count(e.name)) out.elements.push_back(e);
    for (const auto& l : graph.links)
        if (selected.count(l.srcElement) && selected.count(l.sinkElement)) out.links.push_back(l);
    for (const auto& p : graph.exposed) if (selected.count(p.element)) out.exposed.push_back(p);
    // A template instance comes along when all of it is selected; a part of one is plain elements.
    for (const auto& t : graph.instances) {
        const auto inner = templates::members(graph, t.id);
        if (!inner.empty() && std::all_of(inner.begin(), inner.end(), [&](const std::string& m) { return selected.count(m) > 0; }))
            out.instances.push_back(t);
    }
    for (auto& t : out.instances)
        if (!out.instance(t.parent)) t.parent.clear();
    for (auto& e : out.elements)
        if (!out.instance(e.group)) e.group.clear();
    out.nextId = graph.nextId;
    return out;
}
bool GraphFragment::connected(const Graph& graph) const {
    if (elements_.empty()) return false;
    std::set<std::string> selected(elements_.begin(), elements_.end()), seen;
    for (const auto& id : selected) if (!graph.find(id)) return false;
    std::vector<std::string> todo{*selected.begin()};
    while (!todo.empty()) {
        auto id = todo.back(); todo.pop_back();
        if (!seen.insert(id).second) continue;
        for (const auto& link : graph.links) {
            if (link.srcElement == id && selected.count(link.sinkElement)) todo.push_back(link.sinkElement);
            if (link.sinkElement == id && selected.count(link.srcElement)) todo.push_back(link.srcElement);
        }
    }
    return seen == selected;
}
ExecutionCoordinator::ExecutionCoordinator(Pipeline& document, IExecutionBackend& backend) : document_(document), backend_(backend) {}
ExecutionCoordinator::~ExecutionCoordinator() { lifetime_.reset(); backend_.cancel(); }
void ExecutionCoordinator::cancel() { backend_.cancel(); }
void ExecutionCoordinator::supersede() {
    expected_ = {document_.lastChange().executionRevision, ++nextRun_};
    active_ = false;
    backend_.cancel();
}
void ExecutionCoordinator::start(const TraitsFn& traits, Completion completion) {
    // Invalidate the old ticket before cancellation, including synchronous backends.
    expected_ = {document_.lastChange().executionRevision, ++nextRun_};
    if (active_) backend_.cancel();
    auto plan = document_.executionPlan(traits, expected_.run);
    active_ = true;
    std::weak_ptr<int> alive = lifetime_;
    backend_.execute(plan, [this, alive, completion](ExecutionResult result) {
        if (alive.expired() || !active_ || !(result.ticket == expected_)) return;
        active_ = false;
        if (!document_.apply(result, expected_)) return;
        completion(result);
    });
}
} // namespace gps
