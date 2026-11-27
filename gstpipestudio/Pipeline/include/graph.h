// plan.h – the user's decisions as data, and every rule that edits them (SPEC 3 and 4).
// No GStreamer and no Qt in here: the GUI links this library, the engine builds pipelines from it.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace gps {

// The single deliberate exception to "no element specific code" (SPEC 4.4).
inline constexpr const char* kQueueFactory = "queue";
inline constexpr const char* kQueueSinkPad = "sink";  // the queue's pad names belong to the same exception
inline constexpr const char* kQueueSrcPad = "src";

// What the rules need to know about a factory. The caller supplies it:
// the GUI from gps-inspect, the engine from the registry.
struct FactoryTraits {
    bool isSource = false;  // no sink pad template
};
using TraitsFn = std::function<FactoryTraits(const std::string& factory)>;

using Props = std::map<std::string, std::string>;  // property name -> text, only what the user changed

// Pads are document-owned descriptors. Connections remain the single source of
// peer relationships; resolve peers by identity instead of keeping raw pointers.
struct Pad {
    enum class Direction { Source, Sink };
    enum class Presence { Always, Sometimes, Request };
    std::string identity, templateName;
    Direction direction = Direction::Source;
    Presence presence = Presence::Always;
    std::string templateCaps, negotiatedCaps, allowedCaps;
    std::string nativeName;    // what the last run called it ("video_0"), "" when it did not report the pad
    bool unknownData = false;  // the last run could not tell what flows out of it
};
// A pad's address in a graph, and the one encoder of the strings that name it: the canvas and the layout key pads by
// key(), and a link names its sink pad by linkName().
struct PadRef {
    std::string element;
    std::string identity;  // the pad's identity (Pad::identity): "src", "src_%u#0", a Request sink's "sink_%u#1"
    Pad::Direction direction = Pad::Direction::Source;

    bool isSrc() const { return direction == Pad::Direction::Source; }
    std::string templateName() const;  // "sink_%u#1" -> "sink_%u"; a plain pad name is its own template
    std::string linkName() const;      // what a Connection holds: a src pad's identity, a sink pad's templateName()
    std::string key() const;           // "<element>|<identity>|src" or "...|sink"
    static std::optional<PadRef> fromKey(const std::string& key);
    bool operator==(const PadRef& other) const;
    bool operator<(const PadRef& other) const;
};
// A link's address: its src pad, which has at most one link. id() names it to the layout and the canvas.
struct LinkRef {
    PadRef src;
    std::string id() const;  // "<src element>|<src pad>"
};
struct FactoryDefinition {
    bool isSource = false;  // no sink template
    std::vector<Pad> templates;
    bool isSink = false;    // no src template
};

struct Element {
    std::string name;     // unique, "<factory>_<N>" with non-alphanumerics turned into '_'
    std::string factory;
    std::string group;    // the innermost template instance it belongs to (TemplateInstance::id), "" when standalone
    Props props;
    std::vector<Pad> pads; // Derived from factory templates and the current run; not serialized.
    std::map<std::string, int> requestSrc;   // Request src template -> how many pads to ask for before the run
    std::map<std::string, int> requestSink;  // Request sink template -> how many sink pads requested
    double x = 0, y = 0;                     // canvas position: presentation only, never saved (nice alignment places it)
};

struct Connection {
    std::string srcElement;
    std::string srcPad;      // identity: pad name (Always) or "<template>#<k>" (Sometimes and Request)
    std::string sinkElement;
    std::string sinkPad;     // pad name (Always) or template name (Request: every link asks for its own pad)
    std::string caps;        // the user's caps choice for this link, "" = Otomatik
    Props sinkPadProps;      // properties of the sink pad this link creates, e.g. xpos
};

struct ExposedParam {
    std::string name;      // what gst-launch and gps-run call it
    std::string element;   // plan element
    std::string property;  // its property; "caps.<field>" / "<srcpad>.caps.<field>": a field of the caps on its src link
};

// A placed copy of a library template. Its members are ordinary elements whose Element::group names it; a nested
// instance names the one around it in parent. Everything a template instance knows is presentation: the engine never
// reads it, and a conf with instances runs like the same conf without them.
struct TemplateInstance {
    std::string id;            // "<Template>#<n>", unique in its Graph
    std::string templateName;  // the library template it is a copy of
    int revision = 0;          // the library revision it was copied from
    std::string parent;        // the instance around it, "" at the top
    std::vector<ExposedParam> params;          // its parameters: properties and caps fields of its members
    std::map<std::string, std::string> local;  // member element or nested instance -> its name inside the template
    std::string signature;     // content fingerprint at the last sync with the library (templates.h)
};

// "video_%u" and 0 -> "video_%u#0"
std::string padIdentity(const std::string& templateName, int index);
// "video_%u#0" -> true, "video_%u", 0. A plain pad name gives false.
bool splitPadIdentity(const std::string& identity, std::string* templateName, int* index);
// A sink pad name with a number slot is a Request template: any number of links may use it.
bool isTemplateName(const std::string& padName);
// "always" | "request" | anything else (sometimes), as GStreamer's presence is written in catalog and reports.
Pad::Presence presenceOf(const std::string& text);

// What an edit deleted.
struct Removal {
    std::vector<std::string> elements;
    int links = 0;
};

struct Addition {
    std::string element;  // name of the new element, "" when the edit was refused
    Removal removed;
};

class Graph {
public:
    std::string name = "pipeline";
    bool complete = false;  // written by Save: the last update left no free src pad
    int nextId = 0;         // names are never reused
    int revision = 0;       // a template's library revision; 0 for a project
    std::vector<Element> elements;
    std::vector<Connection> links;
    std::vector<ExposedParam> exposed;
    std::vector<TemplateInstance> instances;

    // ---- lookup
    const Element* find(const std::string& name) const;
    Element* find(const std::string& name);
    const TemplateInstance* instance(const std::string& id) const;
    TemplateInstance* instance(const std::string& id);
    std::string sinkIdentity(size_t link) const;
    PadRef srcOf(size_t link) const;   // the pads at the two ends of a link
    PadRef sinkOf(size_t link) const;
    int linkFrom(const std::string& srcElement, const std::string& srcPad) const;  // index, or -1 for a free src pad
    int linkInto(const std::string& sinkElement, const std::string& sinkIdentity) const;  // index, or -1 for a free sink pad
    std::vector<int> linksFromElement(const std::string& srcElement) const;
    std::vector<int> linksInto(const std::string& sinkElement) const;
    bool sinkPadTaken(const std::string& sinkElement, const std::string& sinkPad) const;  // Always pads only
    bool wouldCycle(const std::string& srcElement, const std::string& sinkElement) const;
    // Column of every element: the longest chain of links that leads to it (sources are 0). Empty when links form a cycle.
    std::map<std::string, int> levels() const;
    static std::string ouroborosMessage(const std::string& srcElement, const std::string& sinkElement);
    // Elements reachable from a Source (SRC): an element with an empty sink has an empty src and is not tracked.
    std::vector<std::string> trackedElements(const TraitsFn& traits) const;
    bool isElementTracked(const std::string& name, const TraitsFn& traits) const;
    Graph trackedSubplan(const TraitsFn& traits) const;
    bool empty() const { return elements.empty(); }
    std::vector<std::string> elementNames() const;  // every element's name, in order
    std::vector<std::string> factories() const;     // the factories its elements use, each once, in order
    // Linked pads of an element per Request (or Sometimes) template: src links by their "<template>#<k>" identity,
    // sink links by the template they name. The fewest pads a count may ask for.
    std::map<std::string, int> linkedRequestPads(const std::string& element, Pad::Direction direction) const;
    std::pair<double, double> topLeft() const;  // the top left corner of its elements' positions; (0, 0) when empty

    // ---- additions
    std::string addElement(const std::string& factory, const Props& props = {}, double x = 0, double y = 0);
    std::string addSource(const std::string& factory, const Props& props = {}, double x = 0, double y = 0);
    // New element behind a free src pad, stored at (x, y). Refused ("" ) when the pad is taken.
    std::string addAfter(const std::string& srcElement, const std::string& srcPad, const std::string& factory,
                         const std::string& sinkPad, const Props& props = {}, double x = 0, double y = 0);
    // Free src pad to a free Always sink pad or a new Request pad of an element already in the plan.
    bool linkExisting(const std::string& srcElement, const std::string& srcPad, const std::string& sinkElement,
                      const std::string& sinkPad, const std::string& caps = "", const Props& sinkPadProps = {});

    // ---- edits
    bool setProps(const std::string& element, const Props& props);  // replaces the element's changed properties
    bool setCaps(const std::string& srcElement, const std::string& srcPad, const std::string& caps);
    bool setSinkPadProps(const std::string& srcElement, const std::string& srcPad, const Props& props);
    // Fewer pads than are linked is refused. Linked pads are renumbered #0.., the free ones come after them.
    bool setRequestSrc(const std::string& element, const std::string& templateName, int count);
    bool setRequestSink(const std::string& element, const std::string& templateName, int count);
    void setExposed(const std::vector<ExposedParam>& params);

    // ---- free-form edits (no automatic pruning of downstream elements)
    // Disconnects a single link without deleting downstream elements.
    bool disconnectLink(const std::string& srcElement, const std::string& srcPad);
    // Removes every link whose both ends are among the elements; false when there was none. Nothing else is deleted.
    bool disconnectBetween(const std::set<std::string>& elements);
    // Deletes one or more elements (subgraph). If autoHeal is true and the subgraph had unambiguously 1 external incoming link
    // and 1 external outgoing link, those two external neighbours are automatically linked together.
    bool deleteElements(const std::vector<std::string>& names, bool autoHeal = true, int exitTotalSrcPads = 1);
    bool deleteElement(const std::string& name, bool autoHeal = true, int totalSrcPads = 1);
    // Adapts a dragged element onto an existing link (srcElement.srcPad -> oldSink) without changing any existing links of elementName:
    // - 1 free sink & 1 free src -> E1.src -> E3.sink and E3.src -> E2.sink
    // - 1 free sink & 0 free src -> E1.src -> E3.sink (E2.sink becomes free)
    // - 0 free sink & 1 free src -> E3.src -> E2.sink (E1.src becomes free)
    bool spliceElement(const std::string& elementName, const std::string& linkSrcElement, const std::string& linkSrcPad,
                       const std::vector<std::string>& freeSinkPads, const std::vector<std::string>& freeSrcPads);
    // Adapts a multi-element custom group/chain onto an existing link using its external entry sink pad and exit src pad.
    bool spliceChain(const std::string& linkSrcElement, const std::string& linkSrcPad,
                     const std::vector<std::pair<std::string, std::string>>& freeSinkPads,
                     const std::vector<std::pair<std::string, std::string>>& freeSrcPads);

    // ---- removals. The free canvas keeps what is downstream: only the link, or the element behind it, goes.
    Removal cut(const std::string& srcElement, const std::string& srcPad, const TraitsFn& traits);
    Addition replace(const std::string& srcElement, const std::string& srcPad, const std::string& factory,
                     const std::string& sinkPad, const Props& props, const TraitsFn& traits);

    // ---- the queue shortcut
    std::string insertQueue(const std::string& srcElement, const std::string& srcPad);  // "" if the pad is free
    bool removeQueue(const std::string& queueName);

    // Checks authored topology and references, independently of the installed factories.
    bool validate(std::string* error = nullptr) const;

    // ---- text form
    // Inline: every element, template members too (temporary files, the clipboard, the library's own files).
    // Reference: a project's conf. An unchanged top-level template copy is written like an element, by its template's
    // name, revision and parameter values (as props); a link across its border names the copy and "<member>.<pad>",
    // the member's name inside the template. Like an import: the content comes from the template's own file.
    enum class Form { Inline, Reference };
    std::string toConf(Form form = Form::Inline) const;
    // Finds a referenced template's content by its name; false (and why) when there is none.
    using TemplateSource = std::function<bool(const std::string& templateName, Graph* tmpl, std::string* error)>;
    static bool fromConf(const std::string& text, Graph* out, std::string* error, const TemplateSource& templates = {});
    bool save(const std::string& path, std::string* error) const;  // inline
    // A project's conf in the reference form, and beside it <folder>/templates/<template>.conf for every template it
    // references: the content it uses, so the folder runs on its own (copied to another machine, for instance).
    // A copy goes inline when its content differs from the shared file. Existing references keep their content.
    // beforeWrite can retain the previous version before the main conf is replaced.
    using BeforeProjectWrite = std::function<bool(const std::string& replacement, std::string* error)>;
    bool saveProject(const std::string& path, std::string* error, const BeforeProjectWrite& beforeWrite = {}) const;
    static constexpr const char* kTemplatesFolder = "templates";
    static std::string templatesFolder(const std::string& confPath);  // <folder of the conf>/templates
    // References resolve from <folder of path>/templates, then the folders given, then GPS_TEMPLATE_PATH (':' between).
    static bool load(const std::string& path, Graph* out, std::string* error, const std::vector<std::string>& libraries = {});

private:
    std::vector<std::string> referencable() const;  // the copies the reference form names: unchanged and top-level
    std::string writeConf(Form form, const std::vector<std::string>& referenced) const;
    std::string newName(const std::string& factory);
    void dropElement(const std::string& name, Removal* removed);
    void dropEmptyInstances();
};

// Elements whose src templates are all Request get one pad by default, others none (SPEC 4.2).
// srcTemplates: (template name, presence) of every src template.
std::map<std::string, int> defaultRequestSrc(const std::vector<std::pair<std::string, std::string>>& srcTemplates);

// Strips shell continuations, leading env vars, "gst-launch[-1.0]" and its CLI flags into a clean pipeline string.
std::string normalizeLaunchText(const std::string& text);
// True when text looks like a gst-launch command or pipeline description (and not a .conf file).
bool looksLikeLaunchText(const std::string& text);

}  // namespace gps
