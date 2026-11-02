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

// What the rules need to know about a factory. The caller supplies it:
// the GUI from gps-inspect, the engine from the registry.
struct FactoryTraits {
    bool isSource = false;  // no sink pad template
};
using TraitsFn = std::function<FactoryTraits(const std::string& factory)>;

using Props = std::map<std::string, std::string>;  // property name -> text, only what the user changed

struct PlanElement {
    std::string name;     // unique, "<factory>_<N>" with non-alphanumerics turned into '_'
    std::string factory;
    Props props;
    std::map<std::string, int> requestSrc;  // Request src template -> how many pads to ask for before the run
    double x = 0, y = 0;                    // canvas position, ignored by the engine
};

struct PlanLink {
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

class Plan {
public:
    std::string name = "pipeline";
    bool complete = false;  // written by Save: the last update left no free src pad
    int nextId = 0;         // names are never reused
    std::vector<PlanElement> elements;
    std::vector<PlanLink> links;
    std::vector<ExposedParam> exposed;

    // ---- lookup
    const PlanElement* find(const std::string& name) const;
    PlanElement* find(const std::string& name);
    int linkFrom(const std::string& srcElement, const std::string& srcPad) const;  // index, or -1 for a free src pad
    std::vector<int> linksInto(const std::string& sinkElement) const;
    bool sinkPadTaken(const std::string& sinkElement, const std::string& sinkPad) const;  // Always pads only
    bool wouldCycle(const std::string& srcElement, const std::string& sinkElement) const;
    bool empty() const { return elements.empty(); }

    // ---- additions
    std::string addSource(const std::string& factory, const Props& props = {}, double x = 0, double y = 0);
    // New element behind a free src pad. Refused ("" ) when the pad is taken.
    std::string addAfter(const std::string& srcElement, const std::string& srcPad, const std::string& factory,
                         const std::string& sinkPad, const Props& props = {});
    // Free src pad to a free Always sink pad or a new Request pad of an element already in the plan.
    bool linkExisting(const std::string& srcElement, const std::string& srcPad, const std::string& sinkElement,
                      const std::string& sinkPad, const std::string& caps = "", const Props& sinkPadProps = {});

    // ---- edits
    bool setProps(const std::string& element, const Props& props);  // replaces the element's changed properties
    bool setCaps(const std::string& srcElement, const std::string& srcPad, const std::string& caps);
    bool setSinkPadProps(const std::string& srcElement, const std::string& srcPad, const Props& props);
    // Fewer pads than are linked is refused. Linked pads are renumbered #0.., the free ones come after them.
    bool setRequestSrc(const std::string& element, const std::string& templateName, int count);
    void setExposed(const std::vector<ExposedParam>& params);

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
    static bool fromConf(const std::string& text, Plan* out, std::string* error);
    bool save(const std::string& path, std::string* error) const;
    static bool load(const std::string& path, Plan* out, std::string* error);

private:
    std::string newName(const std::string& factory);
    void dropElement(const std::string& name, Removal* removed);
};

// Elements whose src templates are all Request get one pad by default, others none (SPEC 4.2).
// srcTemplates: (template name, presence) of every src template.
std::map<std::string, int> defaultRequestSrc(const std::vector<std::pair<std::string, std::string>>& srcTemplates);

}  // namespace gps
