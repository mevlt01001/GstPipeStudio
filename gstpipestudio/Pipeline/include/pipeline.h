#pragma once

#include "graph.h"
#include "runtime.h"
#include <cstdint>
#include <memory>
#include <optional>

namespace gps {

// Graph is a value object for transactions and persistence. Pipeline is its sole
// committed owner. Views retain element/pad identities, never pointers across edits.
enum class ChangeKind { Presentation, Execution };
struct Change {
    std::uint64_t revision = 0;
    std::uint64_t executionRevision = 0;
    ChangeKind kind = ChangeKind::Presentation;
};
struct ExecutionTicket {
    std::uint64_t revision = 0, run = 0;
    bool operator==(const ExecutionTicket& other) const { return revision == other.revision && run == other.run; }
};
class ExecutionPlan {
public:
    ExecutionPlan(Graph graph, ExecutionTicket ticket) : graph_(std::move(graph)), ticket_(ticket) {}
    const Graph& graph() const { return graph_; }
    ExecutionTicket ticket() const { return ticket_; }
private:
    Graph graph_;
    ExecutionTicket ticket_;
};
struct ExecutionResult {
    ExecutionTicket ticket;
    UpdateResult report;
};

// All calls and completions are serialized on the owner's thread. A backend may
// dispatch to a process/worker, but must marshal completion back to that thread.
class IExecutionBackend {
public:
    using Completion = std::function<void(ExecutionResult)>;
    virtual ~IExecutionBackend() = default;
    virtual void execute(const ExecutionPlan&, Completion) = 0;
    virtual void cancel() = 0;
};

// Which pads an element has in a graph: its factory's templates, the links that use them and what the last run observed
// (no definition: the observed pads only). Pipeline installs these; NodeShape asks the same of a draft.
std::vector<Pad> padsOf(const Graph& graph, const Element& element, const FactoryDefinition* definition, const UpdateResult& observed);

class Pipeline;
class IAction {
public:
    virtual ~IAction() = default;
    virtual std::string unavailableReason(const Graph&) const { return {}; }
    virtual bool apply(Graph& draft, std::string* error) const = 0;
};
// An adapter for actions composed from existing domain operations. A specialised
// action can implement IAction directly; menus and gestures share GraphEditor.
class EditAction final : public IAction {
public:
    using Operation = std::function<bool(Graph&, std::string*)>;
    explicit EditAction(Operation operation) : operation_(std::move(operation)) {}
    bool apply(Graph& graph, std::string* error) const override { return operation_(graph, error); }
private:
    Operation operation_;
};
class GraphEditor {
public:
    explicit GraphEditor(Pipeline& document) : document_(document) {}
    Graph& begin();
    Graph& draft();
    const Graph& view() const;
    bool editing() const { return draft_.has_value(); }
    bool commit(std::string* error = nullptr);
    void rollback();
    bool execute(const IAction&, std::string* error = nullptr);
    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty() && !editing(); }
    bool canRedo() const { return !redo_.empty() && !editing(); }
    // The number of what the committed graph would write to its file: a commit that changes the conf text and every
    // reset get a new one (moving boxes does not: positions are not saved), undo and redo bring the old one back.
    // A document keeps the number it was saved or loaded at: a different number is an unsaved change.
    std::uint64_t state() const { return state_; }
private:
    friend class Pipeline;
    struct Entry { Graph before, after; std::uint64_t beforeState = 0, afterState = 0; };
    void renumber() { state_ = ++lastState_; }
    Pipeline& document_;
    std::optional<Graph> draft_;
    std::vector<Entry> undo_, redo_;
    std::uint64_t state_ = 0, lastState_ = 0;
};

// Selection is a set of identities, usable for a single element or a subgraph.
class GraphFragment {
public:
    explicit GraphFragment(std::vector<std::string> elements) : elements_(std::move(elements)) {}
    bool connected(const Graph&) const;
    Graph copy(const Graph&) const;
    const std::vector<std::string>& elements() const { return elements_; }
private:
    std::vector<std::string> elements_;
};

class Pipeline {
public:
    // traits tell which elements a source feeds: only those are executed, so only their changes are Execution changes.
    // Without traits every element counts.
    explicit Pipeline(std::string name = "pipeline", TraitsFn traits = {});
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    const Graph& graph() const { return graph_; }
    GraphEditor& editor() { return editor_; }
    const GraphEditor& editor() const { return editor_; }
    void registerFactory(std::string name, FactoryDefinition definition);
    const FactoryDefinition* definition(const std::string& factory) const;  // nullptr when it was not registered
    std::optional<PadRef> peer(const PadRef&) const;  // the pad at the other end of its link
    Change lastChange() const { return change_; }
    bool reset(Graph graph = {}, std::string* error = nullptr);
    ExecutionPlan executionPlan(std::uint64_t run) const;
    // The last validation of this document: the one owner of that result. status "" = not validated.
    const UpdateResult& observations() const { return observations_; }
    bool apply(const ExecutionResult&, ExecutionTicket expected);
    // Nothing to execute (no element, or none fed by a source): not validated when empty, else valid but incomplete.
    void observeIdle();
    static bool validate(const Graph&, std::string* error = nullptr);
private:
    friend class GraphEditor;
    void install(Graph);
    Graph executed(const Graph&) const;  // the part of a graph that runs
    void clearObservations();
    void refreshPads();
    std::map<std::string, FactoryDefinition> factories_;
    TraitsFn traits_;
    Graph graph_;
    GraphEditor editor_;
    Change change_;
    UpdateResult observations_;
    int nextIdentity_ = 0;
};

class ExecutionCoordinator {
public:
    using Completion = std::function<void(const ExecutionResult&)>;
    ExecutionCoordinator(Pipeline& document, IExecutionBackend& backend);
    ~ExecutionCoordinator();
    void start(Completion);
    void cancel();
    void supersede();
    bool active() const { return active_; }
private:
    Pipeline& document_;
    IExecutionBackend& backend_;
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
    ExecutionTicket expected_;
    std::uint64_t nextRun_ = 0;
    bool active_ = false;
};
} // namespace gps
