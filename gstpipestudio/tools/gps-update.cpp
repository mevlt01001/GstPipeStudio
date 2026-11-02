// gps-update – builds the Plan, gives free src pads a temporary end, reads every caps, prints JSON (SPEC 5 and 7).
//   gps-update <plan.conf> [--timeout-ms N]
#include "builder.h"
#include "common.h"

int main(int argc, char** argv) {
    gps::Output out;
    if (!gps::initGst(&argc, &argv)) return 2;
    std::vector<std::string> args(argv + 1, argv + argc);
    gps::UpdateOptions options;
    std::string error;
    if (!gps::takeTimeout(&args, &options.timeoutMs, &error)) return gps::usageError(out, error);
    if (args.size() != 1) return gps::usageError(out, "gps-update <plan.conf> [--timeout-ms N]");

    gps::Plan plan;
    if (!gps::Plan::load(args[0], &plan, &error)) {
        gps::UpdateResult failed;  // a bad file is a result, not a crash (SPEC 7: exit 0 when the work is done)
        failed.status = "error";
        failed.message = args[0] + ": " + error;
        out.json(gps::updateJson(failed));
        return 0;
    }
    out.json(gps::updateJson(gps::update(plan, options, gps::logLine)));
    return 0;
}
