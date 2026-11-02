// common.h – what the three helper programs share: JSON on the real stdout, everything else on stderr.
#pragma once

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <gst/gst.h>

namespace gps {

// Plugins sometimes print to stdout. After this, fd 1 is stderr, and only json() reaches the real stdout.
class Output {
public:
    Output() {
        std::fflush(stdout);
        saved_ = dup(1);
        dup2(2, 1);
    }
    void json(const std::string& text) {
        std::string line = text + "\n";
        const char* p = line.c_str();
        size_t left = line.size();
        while (left > 0) {
            ssize_t n = write(saved_, p, left);
            if (n <= 0) break;
            p += n;
            left -= static_cast<size_t>(n);
        }
    }

private:
    int saved_ = 1;
};

inline void logLine(const std::string& line) { std::fprintf(stderr, "%s\n", line.c_str()); }

// Exit code 2 (SPEC 7): the message on stderr, and a JSON object so a caller that only reads stdout sees it too.
inline int usageError(Output& out, const std::string& message) {
    logLine("kullanim hatasi: " + message);
    std::string escaped;
    for (char c : message) escaped += (c == '"' || c == '\\') ? std::string("\\") + c : std::string(1, c == '\n' ? ' ' : c);
    out.json("{\"error\":\"" + escaped + "\"}");
    return 2;
}

inline bool initGst(int* argc, char*** argv) {
    GError* error = nullptr;
    if (!gst_init_check(argc, argv, &error)) {
        logLine(std::string("GStreamer baslatilamadi: ") + (error ? error->message : "?"));
        return false;
    }
    return true;
}

// "--timeout-ms N" out of argv; N must be a positive whole number.
inline bool takeTimeout(std::vector<std::string>* args, int* timeoutMs, std::string* error) {
    for (size_t i = 0; i < args->size(); ++i) {
        if ((*args)[i] != "--timeout-ms") continue;
        if (i + 1 >= args->size()) {
            *error = "--timeout-ms bir sayi bekler";
            return false;
        }
        char* end = nullptr;
        long value = std::strtol((*args)[i + 1].c_str(), &end, 10);
        if (*end != '\0' || value <= 0 || value > 86400000L) {
            *error = "--timeout-ms icin gecersiz deger: " + (*args)[i + 1];
            return false;
        }
        *timeoutMs = static_cast<int>(value);
        args->erase(args->begin() + static_cast<long>(i), args->begin() + static_cast<long>(i) + 2);
        --i;
    }
    return true;
}

}  // namespace gps
