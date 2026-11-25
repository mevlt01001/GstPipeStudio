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
    Pipeline p("pipeline", traits);
    auto& g = p.editor().begin();
    auto a = g.addElement("source"), b = g.addElement("filter"), c = g.addElement("sink"), loose = g.addElement("filter");
    CHECK(g.linkExisting(a,"src",b,"sink")); CHECK(g.linkExisting(b,"src",c,"sink"));
    CHECK(p.editor().commit());
    CHECK(GraphFragment({a,b,c}).connected(p.graph())); CHECK(!GraphFragment({a,loose}).connected(p.graph()));
    CHECK(!GraphFragment({"absent"}).connected(p.graph()));
    auto fragment = GraphFragment({a,b}).copy(p.graph()); CHECK_EQ(fragment.links.size(), size_t(1));
    const auto execution = p.executionPlan(1); CHECK_EQ(execution.graph().elements.size(), size_t(3));
    CHECK_EQ(p.graph().elements.size(), size_t(4));
    auto execRevision = p.lastChange().executionRevision;
    CHECK(p.editor().execute(EditAction([&](Graph& d, std::string*) { d.find(a)->x = -100000; return true; })));
    CHECK_EQ(p.lastChange().executionRevision, execRevision);
    CHECK(p.editor().undo()); CHECK_EQ(p.lastChange().executionRevision, execRevision);
    // Work no source feeds does not run: adding or changing it is not an Execution change.
    CHECK(p.editor().execute(EditAction([&](Graph& d, std::string*) { d.addElement("filter"); return true; })));
    CHECK(p.lastChange().kind == ChangeKind::Presentation); CHECK_EQ(p.lastChange().executionRevision, execRevision);
    CHECK(p.editor().undo());
    CHECK(p.editor().execute(EditAction([&](Graph& d, std::string*) { return d.setCaps(a,"src","video/x-raw"); })));
    CHECK(p.lastChange().executionRevision > execRevision);
    Graph bad=p.graph();bad.links.push_back(bad.links.front());CHECK(!Pipeline::validate(bad));
    bad=p.graph();bad.find(a)->x=std::numeric_limits<double>::infinity();CHECK(!Pipeline::validate(bad));
    auto saved=p.graph().toConf();Graph loaded;std::string error;
    CHECK(Graph::fromConf(saved,&loaded,&error));CHECK(p.reset(loaded));CHECK_EQ(p.graph().toConf(),saved);
}
void results() {
    Pipeline p("pipeline", traits); DeferredBackend backend;
    p.editor().execute(EditAction([](Graph& g,std::string*) {g.addElement("source");return true;}));
    int received=0;
    {
        ExecutionCoordinator coordinator(p,backend);
        coordinator.start([&](const ExecutionResult&){++received;});
        coordinator.start([&](const ExecutionResult&){++received;});
        backend.finish(0); CHECK_EQ(received,0); CHECK(coordinator.active());
        backend.finish(1); CHECK_EQ(received,1); CHECK(!coordinator.active());
        backend.finish(1); CHECK_EQ(received,1);
        // Mutation after dispatch rejects stale observations, even after undo.
        coordinator.start([&](const ExecutionResult&){++received;});
        p.editor().execute(EditAction([](Graph& g,std::string*){auto f=g.addElement("filter");return g.linkExisting(g.elements[0].name,"src",f,"sink");}));
        p.editor().undo();backend.finish(2);CHECK_EQ(received,1);
        coordinator.start([&](const ExecutionResult&){++received;});
        // Presentation edits do not invalidate a native run.
        p.editor().execute(EditAction([](Graph& g,std::string*){g.elements[0].x=-55;return true;}));
        backend.finish(3);CHECK_EQ(received,2);
        coordinator.start([&](const ExecutionResult&){++received;});
    }
    backend.finish(4);CHECK_EQ(received,2); // callback after coordinator destruction
}
}
// The Pipeline alone decides which pads exist: templates, Request counts, links and the last run's reports.
void pads() {
    Pipeline document;
    auto pad = [](const char* name, Pad::Direction direction, Pad::Presence presence) {
        Pad p; p.identity = p.templateName = name; p.direction = direction; p.presence = presence; return p;
    };
    using D = Pad::Direction; using P = Pad::Presence;
    document.registerFactory("tee", {false, {pad("sink", D::Sink, P::Always), pad("src_%u", D::Source, P::Request)}});
    document.registerFactory("demux", {false, {pad("sink", D::Sink, P::Always), pad("video_%u", D::Source, P::Sometimes)}});
    document.registerFactory("mux", {false, {pad("sink_%u", D::Sink, P::Request), pad("src", D::Source, P::Always)}});
    std::string tee, demux, mux;
    CHECK(document.editor().execute(EditAction([&](Graph& g, std::string*) {
        tee = g.addElement("tee"); demux = g.addElement("demux"); mux = g.addElement("mux");
        g.find(tee)->requestSrc["src_%u"] = 1;
        g.find(mux)->requestSink["sink_%u"] = 1;
        return g.linkExisting(tee, padIdentity("src_%u", 2), mux, "sink_%u") && g.linkExisting(demux, padIdentity("video_%u", 1), mux, "sink_%u");
    })));
    auto identities = [&](const std::string& name) {
        std::vector<std::string> out;
        for (const Pad& p : document.graph().find(name)->pads) out.push_back(p.identity);
        return out;
    };
    // a link to src_%u#2 means three Request pads; a Sometimes pad exists once a link names it; two links, two sink pads
    CHECK(identities(tee) == std::vector<std::string>({"sink", "src_%u#0", "src_%u#1", "src_%u#2"}));
    CHECK(identities(demux) == std::vector<std::string>({"sink", "video_%u#1"}));
    CHECK(identities(mux) == std::vector<std::string>({"sink_%u#0", "sink_%u#1", "src"}));
    CHECK_EQ(document.graph().linkInto(mux, "sink_%u#1"), 1);
    CHECK(document.peer({mux, "sink_%u#0", D::Sink}).value().element == tee);
}
int main(){transactions();invariantsAndFragments();results();pads();return finish("pipeline_test");}
