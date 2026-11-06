// plan.h – the user's decisions as data, and every rule that edits them (SPEC 3 and 4).
// No GStreamer and no Qt in here: the GUI links this library, the engine builds pipelines from it.
#pragma once

#include <functional>
#include <map>
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
};
struct PadAddress {
    std::string element, pad;
    Pad::Direction direction = Pad::Direction::Source;
};
struct FactoryDefinition {
    bool isSource = false;
    std::vector<Pad> templates;
};

struct Element {
    std::string name;     // unique, "<factory>_<N>" with non-alphanumerics turned into '_'
    std::string factory;
    std::string group;    // custom element / group instance id ("<CustomName>#<id>"), "" when standalone
    Props props;
    std::vector<Pad> pads; // Derived from factory templates and the current run; not serialized.
    std::map<std::string, int> requestSrc;   // Request src template -> how many pads to ask for before the run
    std::map<std::string, int> requestSink;  // Request sink template -> how many sink pads requested
    double x = 0, y = 0;                     // canvas position, ignored by the engine
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
    std::string property;  // its property
};

// "video_%u" and 0 -> "video_%u#0"
std::string padIdentity(const std::string& templateName, int index);
// "video_%u#0" -> true, "video_%u", 0. A plain pad name gives false.
bool splitPadIdentity(const std::string& identity, std::string* templateName, int* index);
// A sink pad name with a number slot is a Request template: any number of links may use it.
bool isTemplateName(const std::string& padName);

// What an edit deleted (or would delete, for the preview functions).
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
    std::vector<Element> elements;
    std::vector<Connection> links;
    std::vector<ExposedParam> exposed;

    // ---- lookup
    const Element* find(const std::string& name) const;
    Element* find(const std::string& name);
    std::string sinkIdentity(size_t link) const;
    int linkFrom(const std::string& srcElement, const std::string& srcPad) const;  // index, or -1 for a free src pad
    std::vector<int> linksFromElement(const std::string& srcElement) const;
    std::vector<int> linksInto(const std::string& sinkElement) const;
    bool sinkPadTaken(const std::string& sinkElement, const std::string& sinkPad) const;  // Always pads only
    bool wouldCycle(const std::string& srcElement, const std::string& sinkElement) const;
    static std::string ouroborosMessage(const std::string& srcElement, const std::string& sinkElement);
    // Elements reachable from a Source (SRC): an element with an empty sink has an empty src and is not tracked.
    std::vector<std::string> trackedElements(const TraitsFn& traits) const;
    bool isElementTracked(const std::string& name, const TraitsFn& traits) const;
    Graph trackedSubplan(const TraitsFn& traits) const;
    bool empty() const { return elements.empty(); }

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

    // ---- removals. Every edit ends with prune(), and every one has a preview that changes nothing.
    Removal cut(const std::string& srcElement, const std::string& srcPad, const TraitsFn& traits);
    Removal previewCut(const std::string& srcElement, const std::string& srcPad, const TraitsFn& traits) const;
    Addition replace(const std::string& srcElement, const std::string& srcPad, const std::string& factory,
                     const std::string& sinkPad, const Props& props, const TraitsFn& traits);
    Removal previewReplace(const std::string& srcElement, const std::string& srcPad, const TraitsFn& traits) const;
    Removal deleteSource(const std::string& name, const TraitsFn& traits);
    Removal previewDeleteSource(const std::string& name, const TraitsFn& traits) const;
    // Elements with no linked input left (and that are not sources) go, until none is left.
    Removal prune(const TraitsFn& traits);

    // ---- the queue shortcut
    std::string insertQueue(const std::string& srcElement, const std::string& srcPad);  // "" if the pad is free
    bool removeQueue(const std::string& queueName);

    // ---- text form
    std::string toConf() const;
    static bool fromConf(const std::string& text, Graph* out, std::string* error);
    bool save(const std::string& path, std::string* error) const;
    static bool load(const std::string& path, Graph* out, std::string* error);

private:
    std::string newName(const std::string& factory);
    void dropElement(const std::string& name, Removal* removed);
};

// Elements whose src templates are all Request get one pad by default, others none (SPEC 4.2).
// srcTemplates: (template name, presence) of every src template.
std::map<std::string, int> defaultRequestSrc(const std::vector<std::pair<std::string, std::string>>& srcTemplates);

}  // namespace gps
