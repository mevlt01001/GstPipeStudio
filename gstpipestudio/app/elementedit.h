// elementedit.h – what a PropertyDialog hands to the Controller.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "plan.h"

struct ElementEdit {
    gps::Props props;                          // all changed properties of the element
    std::map<std::string, int> requestSrc;     // Request src template -> pad count
    std::vector<std::string> exposed;          // properties ticked "dışarı aç"
};
