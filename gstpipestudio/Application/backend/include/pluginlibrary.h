// pluginlibrary.h – custom element plugins, the Explorer's and the picker's Plugins section. None can be added yet: it
// lists nothing and says where they will appear. CUSTOM_ELEMENT_PLAN fills it in.
#pragma once

#include "componentprovider.h"

class PluginLibrary : public ComponentProvider {
public:
    using ComponentProvider::ComponentProvider;

    QString kind() const override { return "plugin"; }
    QString title() const override { return "Plugins"; }
    QString emptyHint() const override { return "Custom elements will appear here"; }
    QList<ComponentInfo> components() const override { return {}; }
    gps::Graph peek(const QString&) const override { return {}; }
    std::optional<gps::NodeRef> place(gps::Graph&, const QString&, double, double) const override { return std::nullopt; }
};
