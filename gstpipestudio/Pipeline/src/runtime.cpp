#include "runtime.h"
namespace gps {
bool UpdateResult::complete() const {
    if (status != "ok" || !missing.empty() || !refused.empty()) return false;
    for (const PadReport& p : pads) {
        if (p.isSrc && (p.temporaryEnd || p.unknownData)) return false;
    }
    return true;
}

}
