// plan_test.cpp – every rule of SPEC 3 and 4. Factories whose name starts with "src" are sources.
#include <algorithm>
#include <iostream>

#include "capstext.h"
#include "check.h"
#include "pipeline.h"
#include "plan.h"

using namespace gps;

namespace std {
// CHECK_EQ prints vectors of names.
ostream& operator<<(ostream& out, const vector<string>& v) {
    for (const string& s : v) out << s << " ";
    return out;
}
}  // namespace std

static FactoryTraits traits(const std::string& factory) {
    FactoryTraits t;
    t.isSource = factory.rfind("src", 0) == 0;
    return t;
}
static const TraitsFn T = traits;
using Names = std::vector<std::string>;

static void identities() {
    CHECK_EQ(padIdentity("video_%u", 0), std::string("video_%u#0"));
    std::string t;
    int k = -1;
    CHECK(splitPadIdentity("src_%u#12", &t, &k));
    CHECK_EQ(t, std::string("src_%u"));
    CHECK_EQ(k, 12);
    CHECK(!splitPadIdentity("src", &t, &k));
    CHECK(!splitPadIdentity("src#", &t, &k));
    CHECK(!splitPadIdentity("#1", &t, &k));
    CHECK(!splitPadIdentity("a#b", &t, &k));
    CHECK(isTemplateName("sink_%u"));
    CHECK(isTemplateName("sink_%d"));
    CHECK(!isTemplateName("video_sink"));
}

static void defaults() {
    CHECK(defaultRequestSrc({}).empty());
    auto tee = defaultRequestSrc({{"src_%u", "request"}});
    CHECK_EQ(tee.size(), size_t(1));
    CHECK_EQ(tee["src_%u"], 1);
    CHECK(defaultRequestSrc({{"src", "always"}, {"aux_%u", "request"}}).empty());
    CHECK(defaultRequestSrc({{"video_%u", "sometimes"}}).empty());
}

static void chain() {
    Plan p;
    std::string s = p.addSource("src", {{"num", "1"}});
    CHECK_EQ(s, std::string("src_0"));
    std::string a = p.addAfter(s, "src", "filter", "sink", {}, 280, 5);
    CHECK_EQ(a, std::string("filter_1"));
    std::string b = p.addAfter(a, "src", "input-selector", "sink");
    CHECK_EQ(b, std::string("input_selector_2"));  // dashes become '_'
    CHECK_EQ(p.addAfter(s, "src", "filter", "sink"), std::string(""));  // the src pad is taken
    CHECK_EQ(p.addAfter("nope", "src", "filter", "sink"), std::string(""));
    CHECK_EQ(p.find(a)->x, 280.0);  // the plan stores the position it is given and chooses none
    CHECK_EQ(p.find(a)->y, 5.0);
    CHECK_EQ(p.find(b)->x, 0.0);
    CHECK_EQ(p.linkFrom(s, "src"), 0);
    CHECK_EQ(p.linkFrom(a, "src"), 1);
    CHECK_EQ(p.linkFrom(b, "src"), -1);

    Removal preview = p.previewCut(a, "src", T);
    CHECK(preview.elements.empty());
    CHECK_EQ(preview.links, 1);
    CHECK_EQ(p.elements.size(), size_t(3));  // the preview changed nothing

    Removal r = p.cut(a, "src", T);
    CHECK(r.elements.empty());
    CHECK_EQ(r.links, 1);
    CHECK_EQ(p.elements.size(), size_t(3));  // free canvas: cutting a link keeps the downstream element
    CHECK(p.isElementTracked(a, T));
    CHECK(!p.isElementTracked(b, T));        // disconnected element is no longer tracked

    // Cutting a free pad does nothing.
    Removal none = p.cut(a, "src", T);
    CHECK(none.elements.empty());
    CHECK_EQ(none.links, 0);

    // Names are never reused.
    Plan q;
    std::string first = q.addSource("src");
    q.deleteSource(first, T);
    CHECK_EQ(q.addSource("src"), std::string("src_1"));
}

static void pruning() {
    // Cutting a link only removes the link; downstream elements stay on the free canvas as untracked.
    Plan p;
    std::string s = p.addSource("src");
    std::string a = p.addAfter(s, "src", "f", "sink");
    std::string b = p.addAfter(a, "src", "f", "sink");
    std::string c = p.addAfter(b, "src", "f", "sink");
    Removal r = p.cut(s, "src", T);
    CHECK(r.elements.empty());
    CHECK_EQ(r.links, 1);
    CHECK_EQ(p.elements.size(), size_t(4));
    CHECK(!p.isElementTracked(a, T));
    CHECK(!p.isElementTracked(b, T));
    CHECK(!p.isElementTracked(c, T));

    // Multi-element deletion auto-heals across a connected chain.
    Plan h;
    std::string hs = h.addSource("src");
    std::string ha = h.addAfter(hs, "src", "f", "sink");
    std::string hb = h.addAfter(ha, "src", "f", "sink");
    std::string hc = h.addAfter(hb, "src", "f", "sink");
    CHECK(h.deleteElements({ha, hb}, true));
    CHECK_EQ(h.elements.size(), size_t(2));
    CHECK_EQ(h.links.size(), size_t(1));
    CHECK_EQ(h.links[0].srcElement, hs);
    CHECK_EQ(h.links[0].sinkElement, hc);

    // An element with one linked input stays although another sink pad is free (textoverlay).
    Plan o;
    std::string v = o.addSource("src");
    std::string overlay = o.addAfter(v, "src", "overlay", "video_sink");
    std::string out = o.addAfter(overlay, "src", "sink", "sink");
    CHECK(o.prune(T).elements.empty());
    CHECK(o.find(overlay) && o.find(out));

    // Two sources into a mixer: deleting a source only deletes that source; downstream elements stay on canvas.
    Plan m;
    std::string s1 = m.addSource("src");
    std::string s2 = m.addSource("src");
    std::string mix = m.addAfter(s1, "src", "mixer", "sink_%u");
    CHECK(m.linkExisting(s2, "src", mix, "sink_%u", "video/x-raw, width=(int)160"));
    std::string end = m.addAfter(mix, "src", "sink", "sink");
    Removal one = m.previewDeleteSource(s1, T);
    CHECK_EQ(one.elements, Names({s1}));
    CHECK_EQ(one.links, 1);
    CHECK_EQ(m.elements.size(), size_t(4));
    m.deleteSource(s1, T);
    CHECK(m.find(mix) && m.find(end));
    CHECK(m.isElementTracked(mix, T));
    Removal both = m.deleteSource(s2, T);
    CHECK_EQ(both.elements, Names({s2}));
    CHECK_EQ(m.elements.size(), size_t(2));  // mix and end stay on the free canvas
    CHECK(!m.isElementTracked(mix, T));
    CHECK(!m.isElementTracked(end, T));

    // Only sources can be deleted this way.
    Plan n;
    std::string ns = n.addSource("src");
    std::string nf = n.addAfter(ns, "src", "f", "sink");
    CHECK(n.deleteSource(nf, T).elements.empty());
    CHECK(n.find(nf));
    CHECK(n.deleteSource("nope", T).elements.empty());
}

static void linking() {
    Plan p;
    std::string s1 = p.addSource("src");
    std::string s2 = p.addSource("src");
    std::string f = p.addAfter(s1, "src", "f", "sink");
    // An Always sink pad takes one link.
    CHECK(!p.linkExisting(s2, "src", f, "sink"));
    // A Request template takes as many as needed.
    std::string mix = p.addAfter(f, "src", "mixer", "sink_%u");
    CHECK(p.linkExisting(s2, "src", mix, "sink_%u", "", {{"xpos", "320"}}));
    CHECK_EQ(p.links.back().sinkPadProps.at("xpos"), std::string("320"));
    // The src pad is taken now.
    CHECK(!p.linkExisting(s2, "src", mix, "sink_%u"));
    // No cycles: mixer -> g -> mixer.
    std::string g = p.addAfter(mix, "src", "g", "sink");
    CHECK(p.wouldCycle(g, mix));
    CHECK(!p.linkExisting(g, "src", mix, "sink_%u"));
    CHECK(p.wouldCycle(g, g));
    CHECK(!p.wouldCycle(s2, g));
    // Unknown elements.
    CHECK(!p.linkExisting("nope", "src", mix, "sink_%u"));
    CHECK(!p.linkExisting(g, "src", "nope", "sink"));
}

static void replacing() {
    Plan p;
    std::string s = p.addSource("src");
    std::string a = p.addAfter(s, "src", "f", "sink");
    std::string b = p.addAfter(a, "src", "f", "sink");
    CHECK(p.setCaps(s, "src", "video/x-raw, width=(int)640"));

    CHECK_EQ(p.previewReplace(s, "src", T).elements, Names({a}));
    Addition r = p.replace(s, "src", "other", "video_sink", {{"k", "v"}}, T);
    CHECK_EQ(r.removed.elements, Names({a}));
    CHECK(!r.element.empty());
    CHECK_EQ(p.elements.size(), size_t(3));  // s, b (untracked), and the new element
    int i = p.linkFrom(s, "src");
    CHECK(i >= 0);
    CHECK_EQ(p.links[i].sinkElement, r.element);
    CHECK_EQ(p.links[i].sinkPad, std::string("video_sink"));
    CHECK(p.links[i].caps.empty());  // the old caps choice went with the old link
    CHECK_EQ(p.find(r.element)->props.at("k"), std::string("v"));

    // A free pad has nothing to replace.
    Addition none = p.replace(r.element, "src", "other", "sink", {}, T);
    CHECK(none.element.empty());
    CHECK(none.removed.elements.empty());
    CHECK_EQ(p.elements.size(), size_t(3));
}

static void queues() {
    Plan p;
    std::string s1 = p.addSource("src");
    std::string s2 = p.addSource("src");
    std::string mix = p.addAfter(s1, "src", "mixer", "sink_%u");
    p.links[0].caps = "video/x-raw, width=(int)320";
    p.links[0].sinkPadProps = {{"xpos", "0"}};
    CHECK(p.linkExisting(s2, "src", mix, "sink_%u", "video/x-raw, width=(int)160", {{"xpos", "320"}}));
    CHECK_EQ(p.insertQueue(s2 + "x", "src"), std::string(""));  // no such link
    CHECK_EQ(p.insertQueue(mix, "src"), std::string(""));       // a free pad

    std::string q = p.insertQueue(s1, "src");
    CHECK_EQ(q, std::string("queue_3"));
    CHECK_EQ(p.links.size(), size_t(3));
    // The new link takes the old link's place, so the mixer keeps its pad order.
    CHECK_EQ(p.links[0].srcElement, s1);
    CHECK_EQ(p.links[0].sinkElement, q);
    CHECK_EQ(p.links[0].sinkPad, std::string("sink"));
    CHECK_EQ(p.links[0].caps, std::string("video/x-raw, width=(int)320"));  // the caps choice stays
    CHECK(p.links[0].sinkPadProps.empty());
    CHECK_EQ(p.links[1].srcElement, q);
    CHECK_EQ(p.links[1].sinkElement, mix);
    CHECK_EQ(p.links[1].sinkPadProps.at("xpos"), std::string("0"));  // the pad settings moved on
    CHECK_EQ(p.links[2].srcElement, s2);

    CHECK(!p.removeQueue(mix));  // not a queue
    CHECK(p.removeQueue(q));
    CHECK_EQ(p.links.size(), size_t(2));
    CHECK_EQ(p.links[0].srcElement, s1);
    CHECK_EQ(p.links[0].sinkElement, mix);
    CHECK_EQ(p.links[0].sinkPad, std::string("sink_%u"));
    CHECK_EQ(p.links[0].caps, std::string("video/x-raw, width=(int)320"));
    CHECK_EQ(p.links[0].sinkPadProps.at("xpos"), std::string("0"));
    CHECK(!p.find(q));
    CHECK_EQ(p.elements.size(), size_t(3));  // nothing else was deleted

    // Caps picked on the queue's own output survive when the first link has none.
    std::string q2 = p.insertQueue(s2, "src");
    CHECK(p.setCaps(q2, "src", "video/x-raw, height=(int)90"));
    CHECK(p.setCaps(s2, "src", ""));
    CHECK(p.removeQueue(q2));
    int i = p.linkFrom(s2, "src");
    CHECK_EQ(p.links[i].caps, std::string("video/x-raw, height=(int)90"));

    // A queue with a free output goes alone with its input link.
    std::string q3 = p.insertQueue(s2, "src");
    p.links.erase(p.links.begin() + p.linkFrom(q3, "src"));
    CHECK(p.removeQueue(q3));
    CHECK_EQ(p.linkFrom(s2, "src"), -1);
    CHECK(p.find(s2));
}

static void requestSources() {
    Plan p;
    std::string s = p.addSource("src");
    std::string tee = p.addAfter(s, "src", "tee", "sink");
    CHECK(p.setRequestSrc(tee, "src_%u", 3));
    std::string a = p.addAfter(tee, padIdentity("src_%u", 2), "f", "sink");
    std::string b = p.addAfter(tee, padIdentity("src_%u", 0), "f", "sink");
    CHECK(!a.empty() && !b.empty());
    CHECK(!p.setRequestSrc(tee, "src_%u", 1));  // two pads are linked
    CHECK(p.setRequestSrc(tee, "src_%u", 2));   // linked pads are renumbered #0 and #1
    CHECK_EQ(p.links[p.linkFrom(tee, "src_%u#0")].sinkElement, b);
    CHECK_EQ(p.links[p.linkFrom(tee, "src_%u#1")].sinkElement, a);
    CHECK_EQ(p.linkFrom(tee, "src_%u#2"), -1);
    CHECK_EQ(p.find(tee)->requestSrc.at("src_%u"), 2);
    CHECK(p.setRequestSrc(tee, "aux_%u", 0));
    CHECK(p.find(tee)->requestSrc.count("aux_%u") == 0);
    CHECK(!p.setRequestSrc("nope", "src_%u", 1));
    CHECK(!p.setRequestSrc(tee, "src_%u", -1));
}

static void edits() {
    Plan p;
    std::string s = p.addSource("src");
    std::string a = p.addAfter(s, "src", "f", "sink");
    CHECK(p.setProps(a, {{"a", "1"}, {"b", "2"}}));
    CHECK_EQ(p.find(a)->props.size(), size_t(2));
    CHECK(p.setProps(a, {{"c", "3"}}));
    CHECK_EQ(p.find(a)->props.size(), size_t(1));
    CHECK(!p.setProps("nope", {}));
    CHECK(p.setCaps(s, "src", "x/y"));
    CHECK(!p.setCaps(a, "src", "x/y"));  // a free pad has no link to put caps on
    CHECK(p.setSinkPadProps(s, "src", {{"alpha", "0.5"}}));
    CHECK(!p.setSinkPadProps(a, "src", {}));
    p.setExposed({{"frames", s, "num-buffers"}, {"ghost", "nope", "x"}, {"", s, "y"}});
    CHECK_EQ(p.exposed.size(), size_t(1));
    // An exposed parameter goes with its element.
    p.cut(s, "src", T);
    CHECK_EQ(p.exposed.size(), size_t(1));  // the source stays
    p.deleteSource(s, T);
    CHECK(p.exposed.empty());
}

static void confRoundTrip() {
    Plan p;
    p.name = "demo robot";
    std::string s = p.addSource("filesrc", {{"location", "/data/my file=1.avi"}, {"note", "line1\nline2\\end"}}, 40.5, -80);
    std::string tee = p.addAfter(s, "src", "tee", "sink");
    p.setRequestSrc(tee, "src_%u", 2);
    std::string mix = p.addAfter(tee, "src_%u#0", "compositor", "sink_%u");
    p.setCaps(tee, "src_%u#0", "video/x-raw, width=(int)640, format=(string)\"A B\"");
    p.setSinkPadProps(tee, "src_%u#0", {{"xpos", "320"}, {"alpha", "0.5"}});
    p.setExposed({{"location", s, "location"}});
    p.complete = true;

    std::string text = p.toConf();
    Plan q;
    std::string error;
    CHECK(Plan::fromConf(text, &q, &error));
    CHECK_EQ(error, std::string(""));
    CHECK_EQ(q.toConf(), text);  // the text is a fixed point
    CHECK_EQ(q.name, std::string("demo robot"));
    CHECK(q.complete);
    CHECK_EQ(q.find(s)->props.at("note"), std::string("line1\nline2\\end"));
    CHECK_EQ(q.find(s)->props.at("location"), std::string("/data/my file=1.avi"));
    CHECK_EQ(q.find(s)->x, 40.5);
    CHECK_EQ(q.find(s)->y, -80.0);
    CHECK_EQ(q.links[1].caps, std::string("video/x-raw, width=(int)640, format=(string)\"A B\""));
    CHECK_EQ(q.links[1].sinkPadProps.at("alpha"), std::string("0.5"));
    CHECK_EQ(q.find(tee)->requestSrc.at("src_%u"), 2);
    CHECK_EQ(q.exposed.size(), size_t(1));
    CHECK_EQ(q.exposed[0].property, std::string("location"));
    // New names carry on after the highest one.
    CHECK_EQ(q.addSource("src"), std::string("src_3"));
    (void) mix;

    // File round trip.
    std::string path = "/tmp/gps_plan_test.conf";
    CHECK(p.save(path, &error));
    Plan r;
    CHECK(Plan::load(path, &r, &error));
    CHECK_EQ(r.toConf(), text);
    CHECK(!Plan::load("/no/such/dir/x.conf", &r, &error));
    CHECK(!error.empty());
    std::remove(path.c_str());

    // Unknown sections and keys are ignored.
    Plan u;
    CHECK(Plan::fromConf(text + "\n[future:thing]\nx=1\n", &u, &error));
    CHECK_EQ(u.elements.size(), p.elements.size());
}

static void confErrors() {
    Plan out;
    std::string error;
    const std::string head = "[gstpipestudio]\nversion=1\n";
    CHECK(!Plan::fromConf("", &out, &error));
    CHECK(!Plan::fromConf("[element:a]\nfactory=x\n", &out, &error));                      // no header
    CHECK(!Plan::fromConf("[gstpipestudio]\nversion=2\n", &out, &error));                  // other version
    CHECK(!Plan::fromConf("key=value\n", &out, &error));                                   // before a section
    CHECK(!Plan::fromConf(head + "[element:a]\n", &out, &error));                          // no factory
    CHECK(!Plan::fromConf(head + "[element:a b]\nfactory=x\n", &out, &error));             // bad name
    CHECK(!Plan::fromConf(head + "[element:a]\nfactory=x\n[element:a]\nfactory=y\n", &out, &error));
    CHECK(!Plan::fromConf(head + "[element:a\nfactory=x\n", &out, &error));                // unclosed
    CHECK(!Plan::fromConf(head + "[element:a]\nfactory\n", &out, &error));                 // no '='
    const std::string two = head + "[element:a]\nfactory=src\n[element:b]\nfactory=f\n[element:c]\nfactory=f\n";
    CHECK(Plan::fromConf(two, &out, &error));
    CHECK(!Plan::fromConf(two + "[link:0]\nsrc=a\nsrcpad=src\nsink=zzz\nsinkpad=sink\n", &out, &error));
    CHECK(!Plan::fromConf(two + "[link:0]\nsrc=a\nsrcpad=src\nsink=b\n", &out, &error));  // sinkpad missing
    CHECK(!Plan::fromConf(two + "[link:0]\nsrc=a\nsrcpad=src\nsink=b\nsinkpad=sink\n"
                                "[link:1]\nsrc=a\nsrcpad=src\nsink=c\nsinkpad=sink\n", &out, &error));  // one src pad twice
    CHECK(!Plan::fromConf(two + "[link:0]\nsrc=a\nsrcpad=src\nsink=c\nsinkpad=sink\n"
                                "[link:1]\nsrc=b\nsrcpad=src\nsink=c\nsinkpad=sink\n", &out, &error));  // one sink pad twice
    CHECK(Plan::fromConf(two + "[link:0]\nsrc=a\nsrcpad=src\nsink=c\nsinkpad=sink_%u\n"
                               "[link:1]\nsrc=b\nsrcpad=src\nsink=c\nsinkpad=sink_%u\n", &out, &error));  // a template may repeat
    CHECK(!Plan::fromConf(two + "[link:0]\nsrc=b\nsrcpad=src\nsink=c\nsinkpad=sink\n"
                                "[link:1]\nsrc=c\nsrcpad=src\nsink=b\nsinkpad=sink2\n", &out, &error));  // cycle
    CHECK(!Plan::fromConf(two + "[expose:p]\ntarget=nope.x\n", &out, &error));
    CHECK(!Plan::fromConf(two + "[expose:p]\ntarget=nodot\n", &out, &error));
    CHECK(!error.empty());
    // Windows line ends are fine.
    CHECK(Plan::fromConf("[gstpipestudio]\r\nversion=1\r\n[element:a]\r\nfactory=src\r\n", &out, &error));
    CHECK_EQ(out.find("a")->factory, std::string("src"));
}

static void capsText() {
    CapsChoice c;
    CHECK_EQ(c.toText(), std::string(""));  // no media type: Otomatik
    c.media = "video/x-raw";
    CHECK_EQ(c.toText(), std::string("video/x-raw"));
    c.fields = {{"width", "gint", "640"}, {"format", "gchararray", "I420"}, {"framerate", "GstFraction", "30/1"}};
    CHECK_EQ(c.toText(), std::string("video/x-raw, width=(int)640, format=(string)I420, framerate=(fraction)30/1"));
    c.features = {"memory:NVMM"};
    c.fields = {{"big", "gint64", "5"}, {"flag", "gboolean", "true"}, {"ratio", "gdouble", "1.5"}, {"odd", "SomethingElse", "x"}};
    CHECK_EQ(c.toText(), std::string("video/x-raw(memory:NVMM), big=(gint64)5, flag=(boolean)true, ratio=(double)1.5, odd=x"));
    c.features = {"memory:NVMM", "meta:Overlay"};
    c.fields.clear();
    CHECK_EQ(c.toText(), std::string("video/x-raw(memory:NVMM, meta:Overlay)"));

    CHECK_EQ(capsString("abc_1.2/x:y-z+w"), std::string("abc_1.2/x:y-z+w"));
    CHECK_EQ(capsString(""), std::string("\"\""));
    CHECK_EQ(capsString("a b"), std::string("\"a\\ b\""));
    CHECK_EQ(capsString("rtsp://u@h/s"), std::string("\"rtsp://u@h/s\""));
    CHECK_EQ(capsString("q\"b\\"), std::string("\"q\\\"b\\\\\""));
    CapsChoice s;
    s.media = "application/x-test";
    s.fields = {{"note", "gchararray", "a b"}};
    CHECK_EQ(s.toText(), std::string("application/x-test, note=(string)\"a\\ b\""));
}

static void freeFormAndOopPipeline() {
    // 1. Free-form Plan additions, tracked elements, disconnect without pruning, auto-heal delete, splice, Ouroboros.
    Plan p;
    std::string flt = p.addElement("videoconvert", {}, 100, 100);
    std::string snk = p.addElement("fakesink", {}, 300, 100);
    CHECK(p.linkExisting(flt, "src", snk, "sink"));
    // Neither flt nor snk is fed by a source -> trackedElements is empty!
    CHECK(p.trackedElements(T).empty());
    CHECK(!p.isElementTracked(flt, T));
    CHECK(!p.isElementTracked(snk, T));

    // Once a source is connected to flt, the whole connected chain becomes tracked!
    std::string src = p.addElement("src_cam", {}, 0, 100);
    CHECK(p.linkExisting(src, "src", flt, "sink"));
    CHECK_EQ(p.trackedElements(T), Names({flt, snk, src}));

    // Disconnect link without pruning downstream elements.
    CHECK(p.disconnectLink(src, "src"));
    CHECK_EQ(p.elements.size(), size_t(3));  // flt and snk stay on the canvas!
    CHECK_EQ(p.trackedElements(T), Names({src}));
    CHECK(p.disconnectLink(flt, "src"));     // free snk's sink pad too

    // Reconnect src -> snk and test Splice (Drop flt onto link src -> snk).
    CHECK(p.linkExisting(src, "src", snk, "sink"));
    CHECK(p.spliceElement(flt, src, "src", {"sink"}, {"src"}));
    CHECK_EQ(p.links.size(), size_t(2));
    CHECK_EQ(p.links[0].srcElement, src);
    CHECK_EQ(p.links[0].sinkElement, flt);
    CHECK_EQ(p.links[1].srcElement, flt);
    CHECK_EQ(p.links[1].sinkElement, snk);

    // Delete middle element with Auto-Heal: src -> flt -> snk becomes src -> snk!
    CHECK(p.deleteElement(flt, true, 1));
    CHECK_EQ(p.elements.size(), size_t(2));
    CHECK_EQ(p.links.size(), size_t(1));
    CHECK_EQ(p.links[0].srcElement, src);
    CHECK_EQ(p.links[0].sinkElement, snk);

    // Ouroboros message check.
    CHECK(Plan::ouroborosMessage("a", "a").find("Ouroboros!") == 0);

    // 2. OOP Pipeline -> Element -> Pad -> Caps model + Pipeline.run() trigger.
    Pipeline pipe("oop_demo");
    int runs = 0;
    pipe.setEngineRunner([&](Pipeline*) { ++runs; });

    Element* cam = pipe.addElement("videotestsrc", Element::Role::Src);
    cam->addPad("src", "src", Pad::Direction::Src, Pad::Presence::Always, "video/x-raw");
    Element* conv = pipe.addElement("videoconvert", Element::Role::Filter);
    conv->addPad("sink", "sink", Pad::Direction::Sink, Pad::Presence::Always, "video/x-raw");
    conv->addPad("src", "src", Pad::Direction::Src, Pad::Presence::Always, "video/x-raw");
    Element* out = pipe.addElement("autovideosink", Element::Role::Sink);
    out->addPad("sink", "sink", Pad::Direction::Sink, Pad::Presence::Always, "video/x-raw");

    CHECK(cam->isTracked());
    CHECK(!conv->isTracked());
    CHECK(!out->isTracked());

    std::string err;
    // Self-loop is rejected with Ouroboros!
    CHECK(!pipe.connect(conv->srcPads()[0], conv->sinkPads()[0], &err));
    CHECK(err.find("Ouroboros!") == 0);

    CHECK(pipe.connect(cam->srcPads()[0], conv->sinkPads()[0], &err));
    CHECK(conv->isTracked());
    CHECK(!out->isTracked());
    CHECK(pipe.connect(conv->srcPads()[0], out->sinkPads()[0], &err));
    CHECK(out->isTracked());
    CHECK_EQ(runs, 2);

    // Setting caps triggers Pipeline.run() and associates caps with both linked pads.
    CHECK(pipe.setCaps(cam->srcPads()[0], "video/x-raw, width=(int)1920, height=(int)1080"));
    CHECK_EQ(cam->srcPads()[0]->caps().userFilter, std::string("video/x-raw, width=(int)1920, height=(int)1080"));
    CHECK_EQ(conv->sinkPads()[0]->caps().userFilter, std::string("video/x-raw, width=(int)1920, height=(int)1080"));
    CHECK_EQ(runs, 3);

    // Auto-heal when removing conv from Pipeline.
    CHECK(pipe.removeElement(conv->name(), true));
    CHECK_EQ(cam->srcPads()[0]->linkedPad(), out->sinkPads()[0]);
    CHECK(out->isTracked());
}

int main() {
    identities();
    defaults();
    chain();
    pruning();
    linking();
    replacing();
    queues();
    requestSources();
    edits();
    confRoundTrip();
    confErrors();
    capsText();
    freeFormAndOopPipeline();
    return finish("plan_test");
}
