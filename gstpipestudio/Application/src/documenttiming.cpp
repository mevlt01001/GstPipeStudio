#include "documenttiming.h"

#include <QtGlobal>
#include <limits>

#include "runtime.h"

namespace {

int positiveMilliseconds(const char* name, int fallback, int maximum = std::numeric_limits<int>::max()) {
    bool valid = false;
    const int value = qEnvironmentVariableIntValue(name, &valid);
    return valid && value > 0 && value <= maximum ? value : fallback;
}

}  // namespace

DocumentTiming DocumentTiming::fromEnvironment() {
    DocumentTiming timing;
    timing.validationDebounceMs = positiveMilliseconds("GPS_VALIDATION_DEBOUNCE_MS", timing.validationDebounceMs);
    timing.validationTimeoutMs = positiveMilliseconds("GPS_UPDATE_TIMEOUT_MS", timing.validationTimeoutMs, gps::kMaxTimeoutMs);
    timing.backupDelayMs = positiveMilliseconds("GPS_BACKUP_DELAY_MS", timing.backupDelayMs);
    return timing;
}
