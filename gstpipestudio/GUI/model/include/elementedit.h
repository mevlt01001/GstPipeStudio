// elementedit.h – what a properties page hands to the Controller.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "graph.h"

struct ElementEdit {
    gps::Props props;                          // all changed properties of the element
    std::map<std::string, int> requestSrc;     // Request src template -> pad count
    std::map<std::string, int> requestSink;    // Request sink template -> pad count
    std::vector<std::string> exposed;          // properties ticked "expose"
};
