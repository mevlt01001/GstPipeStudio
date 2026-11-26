#pragma once

struct DocumentTiming {
    int validationDebounceMs = 35;
    int validationTimeoutMs = 600000;
    int backupDelayMs = 1000;

    static DocumentTiming fromEnvironment();
};
