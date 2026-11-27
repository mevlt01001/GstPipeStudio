// catalog.cpp – see catalog.h.
#include "catalog.h"

#include "capstext.h"
#include "graph.h"

namespace gps {
namespace {

std::vector<PadTemplateInfo> templatesOf(const std::vector<PadTemplateInfo>& all, bool src) {
    std::vector<PadTemplateInfo> out;
    for (const PadTemplateInfo& t : all) {
        if (t.isSrc() == src) out.push_back(t);
    }
    return out;
}

std::map<std::string, int> defaultRequests(const std::vector<PadTemplateInfo>& templates, bool patternOnly = false) {
    std::vector<std::pair<std::string, std::string>> list;
    for (const PadTemplateInfo& t : templates) {
        if (patternOnly && !isTemplateName(t.name)) continue;
        list.push_back({t.name, t.presence});
    }
    return defaultRequestSrc(list);
}

}  // namespace

std::vector<PadTemplateInfo> ElementInfo::sinkTemplates() const { return templatesOf(padTemplates, false); }
std::vector<PadTemplateInfo> ElementInfo::srcTemplates() const { return templatesOf(padTemplates, true); }

const PadTemplateInfo* ElementInfo::padTemplate(const std::string& name, const std::string& direction) const {
    for (const PadTemplateInfo& t : padTemplates) {
        if (t.name == name && t.direction == direction) return &t;
    }
    return nullptr;
}

std::vector<PadTemplateInfo> ElementInfo::sinkTemplatesFor(const std::vector<StructureInfo>& allowed) const {
    std::vector<PadTemplateInfo> all = sinkTemplates(), fitting;
    for (const PadTemplateInfo& t : all) {
        if (structuresMeet(allowed, t.structures)) fitting.push_back(t);
    }
    return fitting.empty() ? all : fitting;
}

FactoryDefinition definitionOf(const ElementInfo& info) {
    FactoryDefinition definition;
    definition.isSource = info.entry.isSource;
    definition.isSink = info.entry.isSink;
    for (const PadTemplateInfo& t : info.padTemplates) {
        Pad pad;
        pad.identity = pad.templateName = t.name;
        pad.direction = t.isSrc() ? Pad::Direction::Source : Pad::Direction::Sink;
        pad.presence = presenceOf(t.presence);
        pad.templateCaps = t.caps;
        definition.templates.push_back(std::move(pad));
    }
    return definition;
}

std::map<std::string, int> ElementInfo::defaultRequestSrc() const { return defaultRequests(srcTemplates()); }
std::map<std::string, int> ElementInfo::defaultRequestSink() const { return defaultRequests(sinkTemplates(), true); }

}  // namespace gps
