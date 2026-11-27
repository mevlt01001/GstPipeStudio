#pragma once

#include "pluginspec.h"

class PluginScaffolder {
public:
    static QString source(const PluginSpec& spec);
    static QString cmake(const PluginSpec& spec);
    static QString generateAiPrompt(const PluginSpec& spec, const QString& currentSource);
    static bool updateSpecRegions(const PluginSpec& spec, QString* source, QString* error);
    static bool updateCMakeRegion(const PluginSpec& spec, QString* cmake, QString* error);
};
