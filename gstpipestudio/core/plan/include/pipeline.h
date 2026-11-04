// pipeline.h – Object-Oriented Pipeline -> Element -> Pad (with associated Caps) model.
#pragma once

#include "plan.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gps {

class Element;
class Pad;
class Pipeline;

struct Caps {
    std::string templateCaps;  // Static template caps from factory
    std::string currentCaps;   // Live negotiated caps from GStreamer validation
    std::string allowedCaps;   // Allowed intersection caps on this pad
    std::string userFilter;    // User-selected caps filter on the link

    std::string displayText() const {
        if (!userFilter.empty()) return userFilter;
        if (!currentCaps.empty()) return currentCaps;
        return allowedCaps;
    }
    bool hasFlow() const { return !currentCaps.empty() || !allowedCaps.empty(); }
};

class Pad {
public:
    enum class Direction { Src, Sink };
    enum class Presence { Always, Sometimes, Request };

    Pad(Element* owner, std::string name, std::string templateName, Direction dir, Presence presence,
        std::string templateCaps = "");

    Element* element() const { return element_; }
    const std::string& name() const { return name_; }
    const std::string& templateName() const { return templateName_; }
    Direction direction() const { return direction_; }
    Presence presence() const { return presence_; }
    bool isSrc() const { return direction_ == Direction::Src; }
    bool isSink() const { return direction_ == Direction::Sink; }

    Pad* linkedPad() const { return linkedPad_; }
    bool isLinked() const { return linkedPad_ != nullptr; }

    const Caps& caps() const { return caps_; }
    Caps& caps() { return caps_; }

    const Props& padProps() const { return padProps_; }
    Props& padProps() { return padProps_; }

    // True when data actually flows through this pad (its element is tracked by a source chain).
    bool hasActiveFlow() const;

private:
    friend class Pipeline;
    friend class Element;

    Element* element_ = nullptr;
    std::string name_;
    std::string templateName_;
    Direction direction_ = Direction::Src;
    Presence presence_ = Presence::Always;
    Pad* linkedPad_ = nullptr;
    Caps caps_;
    Props padProps_;
};

class Element {
public:
    enum class Role { Src, Filter, Sink };

    Element(std::string name, std::string factory, Role role, Props props = {}, double x = 0, double y = 0);

    const std::string& name() const { return name_; }
    const std::string& factory() const { return factory_; }
    Role role() const { return role_; }
    bool isSource() const { return role_ == Role::Src; }
    bool isFilter() const { return role_ == Role::Filter; }
    bool isSink() const { return role_ == Role::Sink; }

    const Props& props() const { return props_; }
    Props& props() { return props_; }

    double x() const { return x_; }
    double y() const { return y_; }
    void setPosition(double x, double y) {
        x_ = x;
        y_ = y;
    }

    const std::vector<Pad*>& srcPads() const { return srcRaw_; }
    const std::vector<Pad*>& sinkPads() const { return sinkRaw_; }

    bool canLinkSrc() const { return !srcRaw_.empty(); }
    bool canLinkSink() const { return !sinkRaw_.empty(); }

    Pad* addPad(const std::string& name, const std::string& templateName, Pad::Direction dir, Pad::Presence presence,
                const std::string& templateCaps = "");
    Pad* findPad(const std::string& name, Pad::Direction dir) const;

    // An element is tracked if it is a SRC, or if at least one of its SINK pads is linked to a tracked element's SRC pad.
    // If all SINK pads of a Filter/Sink are empty (or fed by untracked elements), its SRC is also empty and untracked.
    bool isTracked() const;
    bool hasUnlinkedPads() const;
    std::string infoNote() const;

private:
    friend class Pipeline;

    bool isTrackedVisited(std::vector<const Element*>* visited) const;

    std::string name_;
    std::string factory_;
    Role role_ = Role::Filter;
    Props props_;
    double x_ = 0;
    double y_ = 0;
    std::vector<std::unique_ptr<Pad>> ownedPads_;
    std::vector<Pad*> srcRaw_;
    std::vector<Pad*> sinkRaw_;
};

class Pipeline {
public:
    using EngineRunner = std::function<void(Pipeline*)>;

    explicit Pipeline(std::string name = "pipeline");

    const std::string& name() const { return name_; }
    void setName(const std::string& n) { name_ = n; }

    void setEngineRunner(EngineRunner runner) { runner_ = std::move(runner); }
    void run();

    std::vector<Element*> elements() const;
    std::vector<Element*> srcElements() const;
    std::vector<Element*> trackedElements() const;

    Element* findElement(const std::string& name) const;
    Element* addElement(const std::string& factory, Element::Role role, const Props& props = {}, double x = 0, double y = 0);
    bool removeElement(const std::string& name, bool autoHeal = true);

    bool connect(Pad* srcPad, Pad* sinkPad, std::string* error = nullptr);
    bool disconnect(Pad* pad);
    bool setCaps(Pad* srcPad, const std::string& filterCaps);

private:
    bool reaches(const Element* from, const Element* target) const;

    std::string name_;
    int nextId_ = 0;
    std::vector<std::unique_ptr<Element>> elements_;
    EngineRunner runner_;
};

}  // namespace gps

