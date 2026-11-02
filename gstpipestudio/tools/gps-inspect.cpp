// gps-inspect – element information from the registry as JSON (SPEC 7).
//   gps-inspect version | list | element <factory> | compatible [--caps "<caps>"] | caps "<caps>"
#include "common.h"
#include "inspector.h"
#include "json.h"

int main(int argc, char** argv) {
    gps::Output out;
    bool asksVersion = argc == 2 && std::string(argv[1]) == "version";
    std::string why;
    if (!gps::initGst(&argc, &argv, &why)) {
        if (!asksVersion) return 2;
        gps::JsonWriter w;  // the GUI asks "version" first: a broken GStreamer is answered, not left silent
        w.beginObject().field("error", "GStreamer cannot start: " + why + ". Check that GStreamer is installed.").end();
        out.json(w.str());
        return 1;
    }
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) return gps::usageError(out, "gps-inspect version | list | element <factory> | compatible [--caps <caps>] | caps <caps>");
    const std::string& command = args[0];

    if (command == "list" && args.size() == 1) {
        out.json(gps::listJson(gps::listFactories()));
        return 0;
    }
    if (command == "version" && args.size() == 1) {
        out.json(gps::versionJson(gps::listFactories()));
        return 0;
    }
    if (command == "element" && args.size() == 2) {
        gps::ElementInfo info;
        std::string error;
        if (gps::inspect(args[1], &info, &error)) {
            out.json(gps::elementJson(info));
        } else {
            gps::JsonWriter w;
            w.beginObject().field("error", error).end();
            out.json(w.str());
        }
        return 0;
    }
    if (command == "compatible" && (args.size() == 1 || (args.size() == 3 && args[1] == "--caps"))) {
        std::vector<std::string> factories;
        std::string error;
        if (gps::compatibleFactories(args.size() == 3 ? args[2] : "", &factories, &error)) {
            out.json(gps::compatibleJson(factories));
        } else {
            gps::JsonWriter w;
            w.beginObject().field("error", error).end();
            out.json(w.str());
        }
        return 0;
    }
    if (command == "caps" && args.size() == 2) {
        std::string text, error;
        std::vector<gps::StructureInfo> structures;
        bool ok = gps::parseCaps(args[1], &text, &structures, &error);
        out.json(gps::capsJson(ok, ok ? text : error, structures));
        return 0;
    }
    return gps::usageError(out, "unknown command or missing argument: " + command);
}
