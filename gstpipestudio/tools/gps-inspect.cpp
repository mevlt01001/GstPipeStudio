// gps-inspect – element information from the registry as JSON.
#include "common.h"
#include "inspector.h"

#include <algorithm>
#include <iterator>

namespace {

using Arguments = std::vector<std::string>;

struct Command {
    const char* name;
    const char* usageSuffix;
    bool (*accepts)(const Arguments&);
    void (*reply)(gps::Output&, const Arguments&);
};

bool noOperands(const Arguments& args) { return args.size() == 1; }
bool oneOperand(const Arguments& args) { return args.size() == 2; }

const Command commands[]{
    {"version", "", noOperands, [](gps::Output& out, const Arguments&) {
        out.json(gps::toJson(gps::versionInfo(gps::listFactories())));
    }},
    {"list", "", noOperands, [](gps::Output& out, const Arguments&) {
        out.json(gps::toJson(gps::FactoryList{gps::listFactories()}));
    }},
    {"element", " <factory>", oneOperand, [](gps::Output& out, const Arguments& args) {
        gps::ElementInfo info;
        std::string error;
        const bool ok = gps::inspect(args[1], &info, &error);
        out.json(ok ? gps::toJson(info) : gps::toJson(gps::ErrorReply{error}));
    }},
    {"compatible", " [--caps <caps>]", [](const Arguments& args) {
        return noOperands(args) || (args.size() == 3 && args[1] == "--caps");
    }, [](gps::Output& out, const Arguments& args) {
        gps::CompatibleFactories answer;
        std::string error;
        const bool ok = gps::compatibleFactories(noOperands(args) ? "" : args[2], &answer.factories, &error);
        out.json(ok ? gps::toJson(answer) : gps::toJson(gps::ErrorReply{error}));
    }},
    {"caps", " <caps>", oneOperand, [](gps::Output& out, const Arguments& args) {
        gps::CapsResult caps;
        caps.ok = gps::parseCaps(args[1], &caps.text, &caps.structures, &caps.error);
        out.json(gps::toJson(caps));
    }},
};

std::string usage() {
    std::string result = "gps-inspect";
    const char* separator = " ";
    for (const Command& command : commands) {
        result.append(separator).append(command.name).append(command.usageSuffix);
        separator = " | ";
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    gps::Output out;
    const bool asksVersion = argc == 2 && std::string(argv[1]) == "version";
    std::string why;
    if (!gps::initGst(&argc, &argv, &why)) {
        if (!asksVersion) return 2;
        out.json(gps::toJson(gps::ErrorReply{"GStreamer cannot start: " + why + ". Check that GStreamer is installed."}));
        return 1;
    }
    const Arguments args(argv + 1, argv + argc);
    if (args.empty()) return gps::usageError(out, usage());

    const auto command = std::find_if(std::begin(commands), std::end(commands),
                                      [&args](const Command& candidate) { return args.front() == candidate.name; });
    if (command == std::end(commands) || !command->accepts(args))
        return gps::usageError(out, "unknown command or missing argument: " + args.front());

    command->reply(out, args);
    return 0;
}
