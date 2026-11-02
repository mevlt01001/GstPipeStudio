// testutil.h – helpers for the engine tests: fixtures in a temp folder, a short way to write a Plan, pad lookup.
#pragma once

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>

#include <gst/gst.h>

#include "builder.h"
#include "check.h"
#include "plan.h"

namespace gpstest {

// Temp folder with the fixture files; made once per process, removed at exit.
inline const std::string& tempDir() {
    struct Dir {
        std::string path;
        Dir() {
            const char* base = std::getenv("TMPDIR");
            std::string pattern = std::string(base ? base : "/tmp") + "/gps-test-XXXXXX";
            std::string made = pattern;
            if (!mkdtemp(&made[0])) {
                std::perror("mkdtemp");
                std::exit(2);
            }
            path = made;
            std::string command = std::string("'") + GPS_FIXTURE_SCRIPT + "' '" + path + "' >/dev/null 2>&1";
            if (std::system(command.c_str()) != 0) {
                std::printf("make_fixtures.sh failed\n");
                std::exit(2);
            }
        }
        ~Dir() { std::filesystem::remove_all(path); }
    };
    static Dir dir;
    return dir.path;
}

inline size_t fileSize(const std::string& path) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    return ec ? static_cast<size_t>(-1) : static_cast<size_t>(size);
}

// A Plan written in a few readable lines.
class P {
public:
    P& el(const std::string& name, const std::string& factory, gps::Props props = {}) {
        gps::PlanElement e;
        e.name = name;
        e.factory = factory;
        e.props = std::move(props);
        plan.elements.push_back(e);
        return *this;
    }
    P& req(const std::string& element, const std::string& templateName, int count) {
        plan.find(element)->requestSrc[templateName] = count;
        return *this;
    }
    P& link(const std::string& src, const std::string& srcPad, const std::string& sink, const std::string& sinkPad,
            const std::string& caps = "", gps::Props padProps = {}) {
        plan.links.push_back({src, srcPad, sink, sinkPad, caps, std::move(padProps)});
        return *this;
    }
    P& expose(const std::string& name, const std::string& element, const std::string& property) {
        plan.exposed.push_back({name, element, property});
        return *this;
    }
    gps::Plan plan;
};

inline const gps::PadReport* pad(const gps::UpdateResult& r, const std::string& element, const std::string& identity,
                                 bool isSrc = true) {
    for (const gps::PadReport& p : r.pads) {
        if (p.element == element && p.identity == identity && p.isSrc == isSrc) return &p;
    }
    return nullptr;
}

// "ok", or what went wrong in one line: a failing CHECK_EQ then shows the reason.
inline std::string brief(const gps::UpdateResult& r) {
    return r.status == "ok" ? "ok" : r.status + " [" + r.errorElement + "] " + r.message;
}
inline std::string brief(const gps::RunResult& r) {
    return r.status == "eos" ? "eos" : r.status + ": " + r.message;
}

inline bool startsWith(const std::string& text, const std::string& prefix) { return text.rfind(prefix, 0) == 0; }
inline bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Collects the log lines; GPS_TEST_VERBOSE=1 prints them.
struct Log {
    std::string text;
    std::mutex lock;  // pad handlers log from streaming threads
    gps::LogFn fn() {
        return [this](const std::string& line) {
            std::lock_guard<std::mutex> guard(lock);
            text += line + "\n";
            if (std::getenv("GPS_TEST_VERBOSE")) std::fprintf(stderr, "%s\n", line.c_str());
        };
    }
};

inline gps::UpdateResult update(const gps::Plan& plan, int timeoutMs = 10000, Log* log = nullptr) {
    gps::UpdateOptions options;
    options.timeoutMs = timeoutMs;
    Log local;
    return gps::update(plan, options, (log ? log : &local)->fn());
}

}  // namespace gpstest
