#include "pipeline.h"
#include "actions.h"
#include "check.h"
#include <limits>
using namespace gps;

namespace {
TraitsFn traits = [](const std::string& factory) { return FactoryTraits{factory == "source"}; };
struct DeferredBackend : IExecutionBackend {
    std::vector<ExecutionPlan> plans;
    std::vector<Completion> callbacks;
    int cancellations = 0;
    void execute(const ExecutionPlan& plan, Completion done) override { plans.push_back(plan); callbacks.push_back(done); }
    void cancel() override { ++cancellations; }
    void finish(size_t i) { UpdateResult result; result.status = "ok"; callbacks.at(i)({plans.at(i).ticket(), result}); }
};
void transactions() {
    Pipeline document;
    auto& editor = document.editor();
    std::string source, sink;
    EditAction create([&](Graph& graph, std::string*) {
        source = graph.addElement("source"); sink = graph.addElement("sink");
        return graph.linkExisting(source, "src", sink, "sink");
    });
    CHECK(editor.execute(create));
    CHECK_EQ(document.graph().elements.size(), size_t(2));
    CHECK_EQ(document.graph().links.size(), size_t(1));
    CHECK(editor.undo()); CHECK(document.graph().empty());
    auto revision = document.lastChange().revision;
    EditAction fail([](Graph& graph, std::string*) { graph.addElement("discarded"); return false; });
    CHECK(!editor.execute(fail)); CHECK(document.graph().empty()); CHECK(editor.canRedo());
    CHECK_EQ(document.lastChange().revision, revision);
    CHECK(editor.redo()); CHECK_EQ(document.graph().links.size(), size_t(1));
    std::string error;
    EditAction dangling([](Graph& graph, std::string*) { graph.elements.clear(); return true; });
    CHECK(!editor.execute(dangling, &error)); CHECK(!error.empty()); CHECK_EQ(document.graph().elements.size(), size_t(2));
    auto before = document.graph().toConf();
    try {
        editor.execute(EditAction([](Graph& graph, std::string*) -> bool { graph.elements.clear(); throw 1; }));
    } catch (int) {}
    CHECK_EQ(document.graph().toConf(), before); CHECK(!editor.editing());
    CHECK(editor.undo());
    std::string later;
    CHECK(editor.execute(EditAction([&](Graph& g, std::string*) { later = g.addElement("source"); return true; })));
    CHECK(later != source); CHECK(!editor.canRedo());
}
void invariantsAndFragments() {
    Pipeline p;
    auto& g = p.editor().begin();
    auto a = g.addElement("source"), b = g.addElement("filter"), c = g.addElement("sink"), loose = g.addElement("filter");
    CHECK(g.linkExisting(a,"src",b,"sink")); CHECK(g.linkExisting(b,"src",c,"sink"));
    CHECK(p.editor().commit());
    CHECK(GraphFragment({a,b,c}).connected(p.graph())); CHECK(!GraphFragment({a,loose}).connected(p.graph()));
    CHECK(!GraphFragment({"absent"}).connected(p.graph()));
    auto fragment = GraphFragment({a,b}).copy(p.graph()); CHECK_EQ(fragment.links.size(), size_t(1));
    const auto execution = p.executionPlan(traits,1); CHECK_EQ(execution.graph().elements.size(), size_t(3));
    CHECK_EQ(p.graph().elements.size(), size_t(4));
    auto execRevision = p.lastChange().executionRevision;
    CHECK(p.editor().execute(EditAction([&](Graph& d, std::string*) { d.find(a)->x = -100000; return true; })));
    CHECK_EQ(p.lastChange().executionRevision, execRevision);
    CHECK(p.editor().undo()); CHECK_EQ(p.lastChange().executionRevision, execRevision);
    CHECK(p.editor().execute(EditAction([&](Graph& d, std::string*) { return d.setCaps(a,"src","video/x-raw"); })));
    CHECK(p.lastChange().executionRevision > execRevision);
    Graph bad=p.graph();bad.links.push_back(bad.links.front());CHECK(!Pipeline::validate(bad));
    bad=p.graph();bad.find(a)->x=std::numeric_limits<double>::infinity();CHECK(!Pipeline::validate(bad));
    auto saved=p.graph().toConf();Graph loaded;std::string error;
    CHECK(Graph::fromConf(saved,&loaded,&error));CHECK(p.reset(loaded));CHECK_EQ(p.graph().toConf(),saved);
}
void results() {
    Pipeline p; DeferredBackend backend;
    p.editor().execute(EditAction([](Graph& g,std::string*) {g.addElement("source");return true;}));
    int received=0;
    {
        ExecutionCoordinator coordinator(p,backend);
        coordinator.start(traits,[&](const ExecutionResult&){++received;});
        coordinator.start(traits,[&](const ExecutionResult&){++received;});
        backend.finish(0); CHECK_EQ(received,0); CHECK(coordinator.active());
        backend.finish(1); CHECK_EQ(received,1); CHECK(!coordinator.active());
        backend.finish(1); CHECK_EQ(received,1);
        // Mutation after dispatch rejects stale observations, even after undo.
        coordinator.start(traits,[&](const ExecutionResult&){++received;});
        p.editor().execute(EditAction([](Graph& g,std::string*){g.addElement("filter");return true;}));
        p.editor().undo();backend.finish(2);CHECK_EQ(received,1);
        coordinator.start(traits,[&](const ExecutionResult&){++received;});
        // Presentation edits do not invalidate a native run.
        p.editor().execute(EditAction([](Graph& g,std::string*){g.elements[0].x=-55;return true;}));
        backend.finish(3);CHECK_EQ(received,2);
        coordinator.start(traits,[&](const ExecutionResult&){++received;});
    }
    backend.finish(4);CHECK_EQ(received,2); // callback after coordinator destruction
}
}
int main(){transactions();invariantsAndFragments();results();return finish("pipeline_test");}
