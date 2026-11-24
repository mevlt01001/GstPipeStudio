#include "runtime.h"
namespace gps {
bool UpdateResult::complete() const {
    if (status != "ok" || pads.empty() || !missing.empty() || !refused.empty()) return false;
    for (const PadReport& p : pads) {
        if (p.isSrc && (p.temporaryEnd || p.unknownData)) return false;
    }
    return true;
}

const PadReport* UpdateResult::pad(const std::string& element, const std::string& identity, bool isSrc) const {
    for (const PadReport& p : pads) {
        if (p.element == element && p.identity == identity && p.isSrc == isSrc) return &p;
    }
    return nullptr;
}

UpdateResult UpdateResult::failure(std::string status, std::string message) {
    UpdateResult r;
    r.status = std::move(status);
    r.message = std::move(message);
    return r;
}

}
