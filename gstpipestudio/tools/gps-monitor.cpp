// Streams one JSON sample per line; the final line is the normal run result.
#include "builder.h"
#include "common.h"

int main(int argc, char** argv) {
    gps::Output out;
    if (!gps::initGst(&argc, &argv)) return 2;
    std::vector<std::string> args(argv + 1, argv + argc);
    gps::RunOptions options;
    std::string error;
    if (!gps::takeTimeout(&args, &options.timeoutMs, &error)) return gps::usageError(out, error);
    if (args.size() != 1) return gps::usageError(out, "gps-monitor <pipeline.conf> [--timeout-ms N]");
    gps::Graph graph;
    if (!gps::Graph::load(args[0], &graph, &error)) return gps::usageError(out, error);
    gps::installStopHandler();
    options.shouldStop = gps::stopRequested;
    options.onTelemetry = [&out](const gps::TelemetrySample& sample) { out.json(gps::telemetryJson(sample)); };
    auto result = gps::run(graph, {}, options, gps::logLine);
    out.json(gps::runJson(result));
    return result.status == "eos" || result.status == "interrupted" || result.status == "timeout" ? 0 : 1;
}
