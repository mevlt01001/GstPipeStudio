// gps-run – runs the Plan for real, with no temporary ends (SPEC 6 and 7).
//   gps-run <plan.conf> [name=value ...] [--timeout-ms N]
// SIGINT and SIGTERM become EOS, so files get finished properly.
#include <signal.h>

#include "builder.h"
#include "common.h"

static volatile sig_atomic_t g_stop = 0;

int main(int argc, char** argv) {
    gps::Output out;
    if (!gps::initGst(&argc, &argv)) return 2;
    std::vector<std::string> args(argv + 1, argv + argc);
    gps::RunOptions options;
    std::string error;
    if (!gps::takeTimeout(&args, &options.timeoutMs, &error)) return gps::usageError(out, error);

    std::string path;
    std::map<std::string, std::string> params;
    for (const std::string& a : args) {
        size_t eq = a.find('=');
        if (eq != std::string::npos && eq > 0 && a.rfind("--", 0) != 0) params[a.substr(0, eq)] = a.substr(eq + 1);
        else if (path.empty() && a.rfind("--", 0) != 0) path = a;
        else return gps::usageError(out, "gps-run <plan.conf> [ad=deger ...] [--timeout-ms N]");
    }
    if (path.empty()) return gps::usageError(out, "gps-run <plan.conf> [ad=deger ...] [--timeout-ms N]");

    gps::Plan plan;
    if (!gps::Plan::load(path, &plan, &error)) {
        gps::RunResult failed;
        failed.status = "error";
        failed.message = path + ": " + error;
        out.json(gps::runJson(failed));
        return 1;
    }
    for (const auto& p : params) {
        bool known = false;
        for (const gps::ExposedParam& e : plan.exposed) known = known || e.name == p.first;
        if (!known) return gps::usageError(out, "bilinmeyen parametre: " + p.first);
    }

    struct sigaction action = {};
    action.sa_handler = [](int) { g_stop = 1; };
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    options.shouldStop = [] { return g_stop != 0; };

    gps::RunResult result = gps::run(plan, params, options, gps::logLine);
    out.json(gps::runJson(result));
    return result.status == "eos" ? 0 : result.status == "refused" ? 3 : 1;
}
