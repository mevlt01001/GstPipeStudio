// wire.h – the JSON the helper programs print and the GUI reads (SPEC 7). Every field of every answer is named once, in
// wire.cpp, and that one list both writes and reads it.
#pragma once

#include <string>
#include <vector>

#include "catalog.h"
#include "runtime.h"

namespace gps {

// A helper that could not do its job at all (exit code 2).
struct ErrorReply {
    std::string error;
};

// gps-import
struct ImportReply {
    std::string status = "ok";  // ok | error
    std::string message, nativeName;
    int elements = 0, links = 0;
    std::vector<std::string> warnings;
};

// One JSON object. Defined for: ErrorReply, ImportReply, FactoryList, ElementInfo, CompatibleFactories, CapsResult,
// VersionInfo, UpdateResult, RunResult and TelemetrySample.
template <class T>
std::string toJson(const T& value);
// False when the text is not one JSON object (or is another kind of message); missing fields keep the values *out had.
template <class T>
bool fromJson(const std::string& text, T* out);

}  // namespace gps
