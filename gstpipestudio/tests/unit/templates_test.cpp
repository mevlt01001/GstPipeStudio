// templates_test.cpp – template instances in the Graph: placing, nesting, parameters, the library side and the conf.
#include <algorithm>
#include <filesystem>
#include <unistd.h>
#include <iostream>

#include "check.h"
#include "graph.h"
#include "names.h"
#include "pipeline.h"
#include "templates.h"

using namespace gps;
namespace tpl = gps::templates;

namespace std {
ostream& operator<<(ostream& out, const vector<string>& v) {
    for (const string& s : v) out << s << " ";
    return out;
}
}  // namespace std

// A library template: depay -> parse -> dec, parameter dec.bitrate, a caps choice parse -> dec.
static Graph decodeTemplate() {
    Graph t;
    t.name = "Decode";
    t.revision = 3;
    const std::string depay = t.addElement("depay"), parse = t.addElement("parse"), dec = t.addElement("dec", {{"bitrate", "4000"}});
    t.linkExisting(depay, "src", parse, "sink");
    t.linkExisting(parse, "src", dec, "sink", "video/x-h264, width=(int)640");
    t.exposed.push_back({"bitrate", dec, "bitrate"});
    t.exposed.push_back({"width", parse, "caps.width"});
    return t;
}

static void placing() {
    Graph g;
    const std::string src = g.addElement("src");
    const std::string id = tpl::insertTemplate(g, decodeTemplate(), 100, 0);
    CHECK_EQ(id, std::string("Decode#1"));
    CHECK_EQ(tpl::members(g, id).size(), size_t(3));
    const TemplateInstance* t = g.instance(id);
    CHECK(t && t->revision == 3 && t->templateName == "Decode" && t->parent.empty());
    CHECK_EQ(t->params.size(), size_t(2));
    CHECK(!tpl::modified(g, id));
    CHECK(Pipeline::validate(g));
    // Linking from outside to a member is not a change of the template.
    const std::string first = tpl::members(g, id).front();
    CHECK(g.linkExisting(src, "src", first, "sink"));
    CHECK(!tpl::modified(g, id));
    // A second copy gets its own id and its own members.
    const std::string again = tpl::insertTemplate(g, decodeTemplate(), 100, 200);
    CHECK_EQ(again, std::string("Decode#2"));
    CHECK_EQ(g.elements.size(), size_t(7));
}

static void parameters() {
    Graph g;
    const std::string id = tpl::insertTemplate(g, decodeTemplate(), 0, 0);
    const TemplateInstance t = *g.instance(id);
    const ExposedParam bitrate = t.params[0], width = t.params[1];
    CHECK_EQ(tpl::paramValue(g, bitrate), std::string("4000"));
    CHECK_EQ(tpl::paramValue(g, width), std::string("640"));
    // Parameter values are the instance's own: changing them is not a change of the template.
    CHECK(tpl::setParam(g, bitrate, "8000"));
    CHECK(tpl::setParam(g, width, "1280"));
    CHECK_EQ(tpl::paramValue(g, width), std::string("1280"));
    CHECK(!tpl::modified(g, id));
    // A property that is not a parameter is.
    CHECK(g.setProps(bitrate.element, {{"bitrate", "8000"}, {"preset", "fast"}}));
    CHECK(tpl::modified(g, id));
    // An empty value clears the property and the caps field.
    CHECK(tpl::setParam(g, width, ""));
    CHECK_EQ(g.links.back().caps, std::string("video/x-h264"));
    CHECK_EQ(Names(g).param(bitrate), std::string("dec.bitrate"));
    // The user reads factories, numbered from the second one on; the unique names stay underneath.
    const std::string again = tpl::insertTemplate(g, decodeTemplate(), 0, 0);
    CHECK_EQ(Names(g).param(g.instance(again)->params[0]), std::string("dec 2.bitrate"));
    CHECK_EQ(Names(g).element(g.instance(again)->params[0].element), std::string("dec 2"));
    // A copy is called by its template's name, which may change; its id does not.
    g.instance(again)->templateName = "Decoder";
    CHECK_EQ(Names(g).instance(again), std::string("Decoder"));
}

static void structureChanges() {
    Graph g;
    const std::string id = tpl::insertTemplate(g, decodeTemplate(), 0, 0);
    const std::string dec = g.instance(id)->params[0].element;
    Removal removed;
    CHECK(g.deleteElement(dec, false));
    CHECK(tpl::modified(g, id));
    CHECK_EQ(g.instance(id)->params.size(), size_t(1));  // the deleted member's parameter went with it
    // Deleting every member deletes the instance.
    for (const std::string& m : tpl::members(g, id)) g.deleteElement(m, false);
    CHECK(!g.instance(id));
    CHECK(Pipeline::validate(g));
}

static void nesting() {
    Graph g;
    const std::string src = g.addElement("src");
    const std::string decode = tpl::insertTemplate(g, decodeTemplate(), 0, 0);
    const std::string sink = g.addElement("sink");
    const std::vector<std::string> inner = tpl::members(g, decode);
    std::vector<std::string> selection = inner;
    selection.push_back(sink);
    const std::string outer = tpl::wrap(g, selection, "Pipeline", {});
    CHECK_EQ(g.instance(decode)->parent, outer);
    CHECK_EQ(tpl::members(g, outer).size(), size_t(4));
    CHECK_EQ(tpl::nested(g, outer), std::vector<std::string>{decode});
    CHECK_EQ(tpl::chain(g, inner.front()), (std::vector<std::string>{decode, outer}));
    CHECK(!tpl::modified(g, outer));
    CHECK(Pipeline::validate(g));

    // The outer template holds the inner one: extract keeps it as a nested instance with its parameters.
    const Graph lib = tpl::extract(g, outer);
    CHECK_EQ(lib.name, std::string("Pipeline"));
    CHECK_EQ(lib.elements.size(), size_t(4));
    CHECK_EQ(lib.instances.size(), size_t(1));
    CHECK(lib.instances[0].parent.empty());
    CHECK_EQ(lib.instances[0].params.size(), size_t(2));

    // A sanctioned update of the inner one (new revision) leaves both in sync.
    g.setProps(g.instance(decode)->params[0].element, {{"bitrate", "1"}, {"preset", "slow"}});
    CHECK(tpl::modified(g, decode) && tpl::modified(g, outer));
    tpl::sync(g, decode, 4);
    CHECK(!tpl::modified(g, decode) && !tpl::modified(g, outer));
    CHECK_EQ(g.instance(decode)->revision, 4);

    // Detaching the inner one makes its members plain members of the outer one.
    tpl::detach(g, decode);
    CHECK(!g.instance(decode));
    CHECK_EQ(g.find(inner.front())->group, outer);
    CHECK(tpl::modified(g, outer));
    CHECK(Pipeline::validate(g));
    (void)src;
}

static void copies() {
    Graph g;
    const std::string id = tpl::insertTemplate(g, decodeTemplate(), 0, 0);
    // Copying the whole instance copies the instance; the copy is in sync like the original.
    std::vector<std::string> all = tpl::members(g, id);
    Graph clip = GraphFragment(all).copy(g);
    CHECK_EQ(clip.instances.size(), size_t(1));
    const auto names = tpl::insertCopy(g, clip, 40, 40);
    const std::string copy = names.at(id);
    CHECK(copy != id);
    CHECK(!tpl::modified(g, copy));
    CHECK_EQ(g.instance(copy)->params.size(), size_t(2));
    // A part of an instance is plain elements.
    Graph part = GraphFragment({all.front()}).copy(g);
    CHECK(part.instances.empty());
    CHECK(part.elements.front().group.empty());
}

static void confForm() {
    Graph g;
    const std::string src = g.addElement("src");
    const std::string id = tpl::insertTemplate(g, decodeTemplate(), 0, 0);
    g.linkExisting(src, "src", tpl::members(g, id).front(), "sink");
    Graph back;
    std::string error;
    CHECK(Graph::fromConf(g.toConf(), &back, &error));
    CHECK_EQ(error, std::string());
    CHECK_EQ(back.toConf(), g.toConf());
    CHECK(!tpl::modified(back, id));

    // Before 0.4 a group was a tag only: it becomes an instance named after it, in sync with what it holds.
    const std::string old =
        "[gstpipestudio]\nversion=1\n\n[element:a_0]\nfactory=a\ngroup=Chain#3\n\n[element:b_1]\nfactory=b\ngroup=Chain#3\n"
        "\n[link:0]\nsrc=a_0\nsrcpad=src\nsink=b_1\nsinkpad=sink\n";
    Graph migrated;
    CHECK(Graph::fromConf(old, &migrated, &error));
    CHECK(migrated.instance("Chain#3") && migrated.instance("Chain#3")->templateName == "Chain");
    CHECK(!tpl::modified(migrated, "Chain#3"));
    CHECK(Pipeline::validate(migrated));

    // A parameter outside its instance is refused.
    std::string bad = g.toConf() + "\n[template:Stray#1]\ntemplate=Stray\nparam.x=" + src + ".is-live\n";
    CHECK(!Graph::fromConf(bad, &back, &error));
}

static void replacingKeepsMembership() {
    Graph g;
    const std::string src = g.addElement("src");
    const std::string id = tpl::insertTemplate(g, decodeTemplate(), 0, 0);
    const std::vector<std::string> inner = tpl::members(g, id);
    g.linkExisting(src, "src", inner.front(), "sink");
    const Addition added = g.replace(inner[0], "src", "otherparse", "sink", {}, [](const std::string&) { return FactoryTraits{}; });
    CHECK(!added.element.empty());
    CHECK_EQ(g.find(added.element)->group, id);
    CHECK(Pipeline::validate(g));
}

static void updatingFromTheLibrary() {
    Graph g;
    const std::string src = g.addElement("src");
    const std::string id = tpl::insertTemplate(g, decodeTemplate(), 0, 0);
    const std::string first = tpl::members(g, id).front();
    CHECK(g.linkExisting(src, "src", first, "sink"));
    const ExposedParam bitrate = g.instance(id)->params[0];
    CHECK(tpl::setParam(g, bitrate, "8000"));

    // Revision 4 puts a filter between parse and dec.
    Graph newer = decodeTemplate();
    newer.revision = 4;
    const std::string filter = newer.addElement("filter");
    CHECK(newer.disconnectLink("parse_1", "src"));
    CHECK(newer.linkExisting("parse_1", "src", filter, "sink"));
    CHECK(newer.linkExisting(filter, "src", "dec_2", "sink", "video/x-h264, width=(int)640"));
    CHECK(tpl::replaceContent(g, id, newer));

    CHECK_EQ(tpl::members(g, id).size(), size_t(4));
    CHECK_EQ(g.instance(id)->revision, 4);
    CHECK(!tpl::modified(g, id));
    // The link from outside reaches the first element of the new content.
    const int in = g.linkFrom(src, "src");
    CHECK(in >= 0 && tpl::inside(g, g.links[size_t(in)].sinkElement, id));
    CHECK_EQ(g.find(g.links[size_t(in)].sinkElement)->factory, std::string("depay"));
    // The parameter kept its value.
    CHECK_EQ(tpl::paramValue(g, g.instance(id)->params[0]), std::string("8000"));
    CHECK(Pipeline::validate(g));
}

// A project's conf names a template like an element: its content comes from the template's own file, like an import.
static void references() {
    Graph g;
    const std::string src = g.addElement("src");
    const std::string id = tpl::insertTemplate(g, decodeTemplate(), 0, 0);
    const std::vector<std::string> inner = tpl::members(g, id);
    const std::string sink = g.addElement("sink");
    CHECK(g.linkExisting(src, "src", inner.front(), "sink"));
    CHECK(g.linkExisting(inner.back(), "src", sink, "sink", "video/x-raw"));
    CHECK(tpl::setParam(g, g.instance(id)->params[0], "8000"));

    const std::string text = g.toConf(Graph::Form::Reference);
    CHECK(text.find("factory=depay") == std::string::npos);  // the content is not in the project's conf
    CHECK(text.find("[template:Decode#1]\ntemplate=Decode\nrevision=3\nprop.bitrate=8000\n") != std::string::npos);
    CHECK(text.find("sink=Decode#1\nsinkpad=depay_0.sink\n") != std::string::npos);
    CHECK(text.find("src=Decode#1\nsrcpad=dec_2.src\n") != std::string::npos);

    Graph back;
    std::string error;
    auto library = [](const std::string& name, Graph* t, std::string* why) {
        if (name != "Decode") return *why = "unknown", false;
        *t = decodeTemplate();
        return true;
    };
    CHECK(Graph::fromConf(text, &back, &error, library));
    CHECK_EQ(error, std::string());
    CHECK_EQ(tpl::members(back, id).size(), size_t(3));
    CHECK(!tpl::modified(back, id));
    CHECK_EQ(back.instance(id)->revision, 3);
    CHECK_EQ(tpl::paramValue(back, back.instance(id)->params[0]), std::string("8000"));
    const int in = back.linkFrom(src, "src");
    CHECK(in >= 0 && back.find(back.links[size_t(in)].sinkElement)->factory == "depay");
    CHECK(back.linkInto(sink, "sink") >= 0 && back.links[size_t(back.linkInto(sink, "sink"))].caps == "video/x-raw");
    CHECK(Pipeline::validate(back));
    CHECK_EQ(back.toConf(Graph::Form::Reference), text);  // read and written again, the same file

    // Without the template's file the conf cannot be read, and says which template is missing.
    CHECK(!Graph::fromConf(text, &back, &error));
    CHECK(error.find("Decode") != std::string::npos);

    // A changed copy is written whole: nothing of the change is lost.
    CHECK(g.setProps(g.instance(id)->params[0].element, {{"bitrate", "8000"}, {"preset", "fast"}}));
    CHECK(g.toConf(Graph::Form::Reference).find("factory=depay") != std::string::npos);
    CHECK_EQ(tpl::fileName("My cam: 2"), std::string("My_cam__2.conf"));
}

// A saved project keeps a copy of every template it uses in its own templates folder: the folder runs on its own.
static void projectFolder() {
    Graph g;
    const std::string src = g.addElement("src");
    const std::string id = tpl::insertTemplate(g, decodeTemplate(), 0, 0);
    CHECK(g.linkExisting(src, "src", tpl::members(g, id).front(), "sink"));
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / ("gps_project_" + std::to_string(::getpid()));
    std::filesystem::create_directories(folder);
    std::string error;
    CHECK(g.saveProject((folder / "cam.conf").string(), &error));
    CHECK(std::filesystem::exists(folder / "templates" / "Decode.conf"));
    Graph back;
    CHECK(Graph::load((folder / "cam.conf").string(), &back, &error));
    CHECK_EQ(error, std::string());
    CHECK_EQ(tpl::members(back, id).size(), size_t(3));
    CHECK(!tpl::modified(back, id));
    // Another conf of the folder holding an older revision writes its copy inline: cam.conf keeps revision 3.
    Graph older;
    const std::string old = tpl::insertTemplate(older, decodeTemplate(), 0, 0);
    tpl::sync(older, old, 2);
    CHECK(older.saveProject((folder / "door.conf").string(), &error));
    CHECK(Graph::load((folder / "cam.conf").string(), &back, &error));
    CHECK_EQ(back.instance(id)->revision, 3);
    CHECK(Graph::load((folder / "door.conf").string(), &back, &error));
    CHECK_EQ(back.instance(old)->revision, 2);
    CHECK(!tpl::modified(back, old));
    // Without the copy it is not found (no library given).
    std::filesystem::remove_all(folder / "templates");
    CHECK(!Graph::load((folder / "cam.conf").string(), &back, &error));
    std::filesystem::remove_all(folder);
}

int main() {
    placing();
    parameters();
    structureChanges();
    nesting();
    copies();
    confForm();
    replacingKeepsMembership();
    updatingFromTheLibrary();
    references();
    projectFolder();
    return finish("templates_test");
}
