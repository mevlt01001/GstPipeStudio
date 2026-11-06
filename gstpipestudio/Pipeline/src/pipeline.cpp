#include "pipeline.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace gps {
namespace {
std::string executionKey(Graph graph) {
    graph.name.clear(); graph.complete = false; graph.nextId = 0;
    for (auto& e : graph.elements) { e.x = e.y = 0; e.group.clear(); }
    return graph.toConf();
}
bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}
}

Pipeline::Pipeline(std::string name) : editor_(*this) { graph_.name = std::move(name); observations_.status.clear(); }
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
        observations_ = {}; observations_.status.clear();
    }
    refreshPads();
}
bool Pipeline::reset(Graph graph, std::string* error) {
    if (!validate(graph, error)) return false;
    editor_.rollback(); editor_.undo_.clear(); editor_.redo_.clear();
    install(std::move(graph));
    // A new document invalidates every run even if its serialized contents match.
    ++change_.executionRevision;
    observations_ = {}; observations_.status.clear();
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
void Pipeline::registerFactory(std::string name, FactoryDefinition definition) {
    factories_[std::move(name)] = std::move(definition);
    refreshPads();
}
void Pipeline::refreshPads() {
    for (auto& element : graph_.elements) {
        element.pads.clear();
        auto factory = factories_.find(element.factory);
        if (factory != factories_.end()) for (const auto& descriptor : factory->second.templates) {
            if (descriptor.presence == Pad::Presence::Always) element.pads.push_back(descriptor);
            else if (descriptor.presence == Pad::Presence::Request) {
                const auto& counts = descriptor.direction == Pad::Direction::Source ? element.requestSrc : element.requestSink;
                auto count = counts.find(descriptor.templateName);
                int total = count == counts.end() ? 0 : count->second;
                if (descriptor.direction == Pad::Direction::Sink) {
                    int linked = 0;
                    for (const auto& edge : graph_.links)
                        if (edge.sinkElement == element.name && edge.sinkPad == descriptor.templateName) ++linked;
                    total = std::max(total, linked);
                }
                for (int i=0; i<total; ++i) { auto pad=descriptor; pad.identity=padIdentity(pad.templateName,i); element.pads.push_back(std::move(pad)); }
            }
        }
        for (const auto& report : observations_.pads) {
            if (report.element != element.name) continue;
            const auto direction = report.isSrc ? Pad::Direction::Source : Pad::Direction::Sink;
            auto found = std::find_if(element.pads.begin(),element.pads.end(),[&](const Pad& p){return p.identity==report.identity && p.direction==direction;});
            if (found == element.pads.end()) {
                Pad pad; pad.identity=report.identity; pad.templateName=report.templateName; pad.direction=direction;
                pad.presence=report.presence=="always" ? Pad::Presence::Always : report.presence=="request" ? Pad::Presence::Request : Pad::Presence::Sometimes;
                element.pads.push_back(std::move(pad)); found=element.pads.end()-1;
            }
            found->negotiatedCaps=report.caps; found->allowedCaps=report.allowed;
        }
    }
}
std::optional<PadAddress> Pipeline::peer(const PadAddress& address) const {
    for (size_t i=0; i<graph_.links.size(); ++i) {
        const auto& edge=graph_.links[i];
        if (address.direction==Pad::Direction::Source && edge.srcElement==address.element && edge.srcPad==address.pad)
            return PadAddress{edge.sinkElement,graph_.sinkIdentity(i),Pad::Direction::Sink};
        if (address.direction==Pad::Direction::Sink && edge.sinkElement==address.element && graph_.sinkIdentity(i)==address.pad)
            return PadAddress{edge.srcElement,edge.srcPad,Pad::Direction::Source};
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
