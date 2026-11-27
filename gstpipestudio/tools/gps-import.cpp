// Import a gst-launch pipeline description through GStreamer's parser and pad API.
#include "common.h"
#include "importer.h"
#include <cstdlib>

int main(int argc, char** argv) {
    gps::Output output;
    if (!gps::initGst(&argc, &argv)) return 2;
    std::string path, description, error;
    int probeMs = 0;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc) path = argv[++i];
        else if (arg == "--probe-ms" && i + 1 < argc) {
            std::string number = argv[++i];
            char* end = nullptr;
            long parsed = std::strtol(number.c_str(), &end, 10);
            if (!end || *end || parsed < 0 || parsed > 10000) return gps::usageError(output, "--probe-ms must be 0–10000");
            probeMs = static_cast<int>(parsed);
        } else if (!description.empty() || arg.rfind("--", 0) == 0) {
            return gps::usageError(output, "gps-import --output <pipeline.conf> [--probe-ms N] <pipeline description>");
        } else description = arg;
    }
    if (path.empty() || description.empty()) return gps::usageError(output,
        "gps-import --output <pipeline.conf> [--probe-ms N] <pipeline description>");
    gps::ImportResult imported;
    bool ok = gps::importLaunch(description, probeMs, &imported, &error);
    if (ok) ok = imported.graph.save(path, &error);
    gps::ImportReply reply;
    reply.status = ok ? "ok" : "error";
    reply.message = error;
    if (ok) {
        reply.nativeName = imported.nativeName;
        reply.elements = static_cast<int>(imported.graph.elements.size());
        reply.links = static_cast<int>(imported.graph.links.size());
        reply.warnings = imported.warnings;
    }
    output.json(gps::toJson(reply));
    return ok ? 0 : 1;
}
