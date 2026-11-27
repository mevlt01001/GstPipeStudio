#include "pipeline.h"
#include "nodes.h"
#include "templates.h"
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
    Graph bad=p.graph();bad.links.push_back(bad.links.front());CHECK(!bad.validate());
    bad=p.graph();bad.find(a)->x=std::numeric_limits<double>::infinity();CHECK(!bad.validate());
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
// The editor numbers what is committed: undo back to a number is the same state, a new edit after an undo is not.
void savePoint() {
    Pipeline document;
    auto& editor = document.editor();
    auto add = [&] { return editor.execute(EditAction([](Graph& g, std::string*) { g.addElement("source"); return true; })); };
    const auto loaded = editor.state();
    CHECK(add());
    const auto saved = editor.state();
    CHECK(saved != loaded);
    CHECK(add()); CHECK(editor.state() != saved);
    CHECK(editor.undo()); CHECK_EQ(editor.state(), saved);
    CHECK(editor.redo()); CHECK(editor.state() != saved);
    CHECK(editor.undo()); CHECK(add()); CHECK(editor.state() != saved);  // the same count of steps, another state
    // Moving a box changes nothing the file holds: the number stays.
    const auto before = editor.state();
    CHECK(editor.execute(EditAction([](Graph& g, std::string*) { g.elements.front().x += 50; return true; })));
    CHECK_EQ(editor.state(), before);
    CHECK(document.reset()); CHECK(editor.state() != saved); CHECK(editor.state() != loaded);
}
// A pad's key is the one string form of its address: it reads back as the same pad, and a link names it by linkName().
void padRefs() {
    using D = Pad::Direction;
    const PadRef src{"tee_1", "src_%u#2", D::Source}, sink{"mix_3", "sink_%u#1", D::Sink}, plain{"conv_2", "sink", D::Sink};
    for (const PadRef& p : {src, sink, plain}) CHECK(PadRef::fromKey(p.key()).value() == p);
    CHECK_EQ(src.key(), std::string("tee_1|src_%u#2|src"));
    CHECK_EQ(src.linkName(), std::string("src_%u#2"));   // a src pad is linked by its identity
    CHECK_EQ(sink.linkName(), std::string("sink_%u"));   // a Request sink pad by its template
    CHECK_EQ(plain.linkName(), std::string("sink"));
    CHECK(!PadRef::fromKey("tee_1|src").has_value());
    CHECK(!PadRef::fromKey("tee_1|src|up").has_value());
    CHECK_EQ(LinkRef{src}.id(), std::string("tee_1|src_%u#2"));
    Graph g;
    const std::string a = g.addElement("a"), b = g.addElement("b");
    CHECK(g.linkExisting(a, "src", b, "sink_%u"));
    CHECK(g.srcOf(0) == (PadRef{a, "src", D::Source}));
    CHECK(g.sinkOf(0) == (PadRef{b, "sink_%u#0", D::Sink}));
}
// A node's shape comes from the definitions and the links alone, so a draft that no Pipeline holds answers too: a copy's
// way in is its first free sink pad, else a new pad of a Request sink template; with no way in it is a source.
void nodeShapes() {
    using D = Pad::Direction; using P = Pad::Presence;
    auto pad = [](const char* name, D direction, P presence) {
        Pad p; p.identity = p.templateName = name; p.direction = direction; p.presence = presence; return p;
    };
    const std::map<std::string, FactoryDefinition> defs = {
        {"src", {true, {pad("src", D::Source, P::Always)}}},
        {"conv", {false, {pad("sink", D::Sink, P::Always), pad("src", D::Source, P::Always)}}},
        {"mux", {false, {pad("sink_%u", D::Sink, P::Request), pad("src", D::Source, P::Always)}}},
        {"sink", {false, {pad("sink", D::Sink, P::Always)}, true}},
    };
    Graph g;
    const std::string s = g.addElement("src"), c = g.addElement("conv"), k = g.addElement("sink");
    const std::string m = g.addElement("mux"), k2 = g.addElement("sink");
    CHECK(g.linkExisting(s, "src", c, "sink") && g.linkExisting(m, "src", k2, "sink"));
    const NodeRef head = NodeRef::instance(templates::wrap(g, {s, c}, "Head", {}));
    const NodeRef tail = NodeRef::instance(templates::wrap(g, {k}, "Tail", {}));
    const NodeRef mixer = NodeRef::instance(templates::wrap(g, {m, k2}, "Mixer", {}));
    const NodeShape shape(g, [&](const std::string& f) { auto it = defs.find(f); return it == defs.end() ? nullptr : &it->second; });

    CHECK(shape.role(head) == Role::Source);
    CHECK(!shape.firstFree(head, D::Sink).has_value());
    CHECK(shape.firstFree(head, D::Source).value() == (PadRef{c, "src", D::Source}));
    CHECK(shape.role(tail) == Role::Sink);
    CHECK(shape.firstFree(tail, D::Sink).value() == (PadRef{k, "sink", D::Sink}));
    CHECK(shape.role(mixer) == Role::Sink);  // a Request sink is a way in
    const PadRef request = shape.firstFree(mixer, D::Sink).value();
    CHECK(request.element == m && request.linkName() == "sink_%u");
    CHECK(shape.role(NodeRef::element(c)) == Role::Filter);
    CHECK(shape.members(head) == std::vector<std::string>({s, c}));
    // Linked, the pad is no longer free, on the same graph the shape reads.
    CHECK(g.linkExisting(c, "src", k, "sink"));
    CHECK(!shape.firstFree(tail, D::Sink).has_value());
    CHECK(shape.role(head) == Role::Source && shape.ports(head).srcs.front().link >= 0);

    // Exact-named Request sink templates (no '%'): singleton identity without '#0', and taken once linked.
    Pipeline exactDoc;
    exactDoc.registerFactory("src", {true, {pad("src", D::Source, P::Always)}});
    exactDoc.registerFactory("ovl", {false, {pad("sink_video", D::Sink, P::Request), pad("sink_param", D::Sink, P::Request), pad("src", D::Source, P::Always)}});
    std::string s1, s2, ovl;
    CHECK(exactDoc.editor().execute(EditAction([&](Graph& eg, std::string*) {
        s1 = eg.addElement("src");
        s2 = eg.addElement("src");
        ovl = eg.addElement("ovl");
        return eg.linkExisting(s1, "src", ovl, "sink_video");
    })));
    std::vector<std::string> ovlIds;
    for (const Pad& p : exactDoc.graph().find(ovl)->pads) ovlIds.push_back(p.identity);
    CHECK(ovlIds == std::vector<std::string>({"sink_video", "sink_param", "src"}));
    CHECK(exactDoc.graph().sinkOf(0) == (PadRef{ovl, "sink_video", D::Sink}));
    CHECK_EQ(exactDoc.graph().linkInto(ovl, "sink_video"), 0);
    CHECK(exactDoc.peer({s1, "src", D::Source}) == (PadRef{ovl, "sink_video", D::Sink}));
    CHECK(exactDoc.peer({ovl, "sink_video", D::Sink}) == (PadRef{s1, "src", D::Source}));
    const NodeShape exactShape(exactDoc.graph(), exactDoc);
    CHECK(exactShape.firstFree(NodeRef::element(ovl), D::Sink) == (PadRef{ovl, "sink_param", D::Sink}));
    CHECK(!exactDoc.editor().execute(EditAction([&](Graph& eg, std::string*) {
        return eg.linkExisting(s2, "src", ovl, "sink_video");
    })));
}

void launchTextHelpers() {
    CHECK_EQ(normalizeLaunchText("GST_PLUGIN_PATH=/tmp/p gst-launch-1.0 -e -v \\\n  videotestsrc ! fakesink"),
             std::string("videotestsrc ! fakesink"));
    CHECK(looksLikeLaunchText("GST_DEBUG=3 gst-launch-1.0 -e videotestsrc ! fakesink"));
    CHECK(looksLikeLaunchText("videotestsrc ! queue ! autovideosink"));
    CHECK(!looksLikeLaunchText("[pipeline]\nformat=1\n\nelement a videotestsrc"));
    CHECK(!looksLikeLaunchText("# comment"));
}
int main(){transactions();invariantsAndFragments();results();pads();savePoint();padRefs();nodeShapes();launchTextHelpers();return finish("pipeline_test");}
