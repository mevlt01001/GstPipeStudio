#include "documenttiming.h"

#include <QtGlobal>

namespace {

int positiveMilliseconds(const char* name, int fallback) {
    bool valid = false;
    const int value = qEnvironmentVariableIntValue(name, &valid);
    return valid && value > 0 ? value : fallback;
}

}  // namespace

DocumentTiming DocumentTiming::fromEnvironment() {
    DocumentTiming timing;
    timing.validationDebounceMs = positiveMilliseconds("GPS_VALIDATION_DEBOUNCE_MS", timing.validationDebounceMs);
    timing.validationTimeoutMs = positiveMilliseconds("GPS_UPDATE_TIMEOUT_MS", timing.validationTimeoutMs);
    timing.backupDelayMs = positiveMilliseconds("GPS_BACKUP_DELAY_MS", timing.backupDelayMs);
    return timing;
}
