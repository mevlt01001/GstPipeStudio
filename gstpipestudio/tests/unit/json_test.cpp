// json_test – the JSON writer and reader, and the helper answers of wire.h written and read back. No GStreamer needed.
#include "check.h"
#include "json.h"
#include "wire.h"

using gps::JsonWriter;
using gps::jsonEscape;

int main() {
    // escaping
    CHECK_EQ(jsonEscape("plain"), "plain");
    CHECK_EQ(jsonEscape("a\"b"), "a\\\"b");
    CHECK_EQ(jsonEscape("a\\b"), "a\\\\b");
    CHECK_EQ(jsonEscape("l1\nl2\r\t"), "l1\\nl2\\r\\t");
    CHECK_EQ(jsonEscape(std::string("x\x01y", 3)), "x\\u0001y");
    CHECK_EQ(jsonEscape(std::string("x\0y", 3)), "x\\u0000y");
    CHECK_EQ(jsonEscape("\b\f"), "\\b\\f");
    CHECK_EQ(jsonEscape("éñ ß 日本 😀"), "éñ ß 日本 😀");  // valid UTF-8 passes through untouched
    CHECK_EQ(jsonEscape("a\xff" "b"), "a\xEF\xBF\xBD" "b");        // invalid byte
    CHECK_EQ(jsonEscape("a\xC3"), "a\xEF\xBF\xBD");                // cut sequence at the end
    CHECK_EQ(jsonEscape("\xC0\x80"), "\xEF\xBF\xBD\xEF\xBF\xBD");  // overlong NUL
    CHECK_EQ(jsonEscape("\xED\xA0\x80"), "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");  // surrogate

    // structure
    {
        JsonWriter w;
        w.beginObject().field("a", "x").field("n", 3).field("f", 1.5).field("b", true);
        w.key("list").beginArray().string("p").integer(-2).boolean(false).null().end();
        w.key("empty").beginArray().end();
        w.key("obj").beginObject().field("k", "v").end();
        w.end();
        CHECK_EQ(w.str(), "{\"a\":\"x\",\"n\":3,\"f\":1.5,\"b\":true,\"list\":[\"p\",-2,false,null],\"empty\":[],\"obj\":{\"k\":\"v\"}}");
    }
    {
        JsonWriter w;
        w.beginArray().beginObject().field("a", 1).end().beginObject().field("b", 2).end().end();
        CHECK_EQ(w.str(), "[{\"a\":1},{\"b\":2}]");
    }
    {
        JsonWriter w;
        w.beginObject().key("k\"ey").string("v\n").end();  // keys are escaped too
        CHECK_EQ(w.str(), "{\"k\\\"ey\":\"v\\n\"}");
    }

    // numbers
    {
        JsonWriter w;
        w.beginArray().number(0.1).number(12.3).number(1e20).number(-0.5).number(0.0 / 0.0).number(1.0 / 0.0).integer(9007199254740993LL).end();
        CHECK_EQ(w.str(), "[0.1,12.3,1e+20,-0.5,null,null,9007199254740993]");
    }

    // reading
    {
        gps::JsonValue v;
        CHECK(gps::parseJson(" {\"a\": [1, 2.5e1, true, null], \"s\": \"x\\\"y\\u00e9\\ud83d\\ude00\", \"o\": {}} ", &v));
        CHECK(v.type == gps::JsonValue::Type::Object && v.members.size() == 3);
        const gps::JsonValue* a = v.find("a");
        CHECK(a && a->items.size() == 4 && a->items[1].number == 25 && a->items[1].text == "2.5e1");
        CHECK(a && a->items[2].boolean && a->items[3].type == gps::JsonValue::Type::Null);
        CHECK(v.find("s") && v.find("s")->text == "x\"y\xC3\xA9\xF0\x9F\x98\x80");  // escapes, \u00e9, a surrogate pair
        CHECK(!v.find("missing"));
        for (const char* bad : {"", "{", "{\"a\":}", "[1,]", "{\"a\":1} x", "\"open", "{\"a\" 1}", "\"\\ud83d\""}) CHECK(!gps::parseJson(bad, &v));
        CHECK_EQ(gps::jsonNumber("1.5"), 1.5);
        CHECK_EQ(gps::jsonNumber("x"), 0.0);
    }

    // wire.h: every answer reads back as it was written
    {
        gps::ElementInfo info;
        info.entry.factory = "tee";
        info.entry.rank = 256;
        info.entry.hasRequestSrc = true;
        info.description = "1-to-N \"pipe\" fitting";
        gps::PadTemplateInfo t;
        t.name = "src_%u"; t.direction = "src"; t.presence = "request"; t.caps = "ANY";
        t.structures.push_back({"video/x-raw", {"memory:NVMM"}, {{"width", "gint", "range", {"16", "4096"}}}});
        info.padTemplates.push_back(t);
        gps::PropertyInfo p;
        p.name = "num-src-pads"; p.defaultValue = "0"; p.readable = true;
        p.enumValues.push_back({"a", "A", "1"});
        info.properties.push_back(p);
        gps::ElementInfo back;
        CHECK(gps::fromJson(gps::toJson(info), &back));
        CHECK_EQ(gps::toJson(back), gps::toJson(info));
        CHECK(back.entry.factory == "tee" && back.entry.rank == 256 && back.entry.hasRequestSrc);
        CHECK(back.padTemplates.size() == 1 && back.padTemplates[0].structures[0].fields[0].values[1] == "4096");
        CHECK(back.properties.size() == 1 && back.properties[0].enumValues[0].value == "1");
        CHECK(gps::toJson(info).find("\"factory\":\"tee\",") == 1);  // the factory row stays flat in the object
    }
    {
        gps::UpdateResult r;
        r.status = "ok";
        gps::PadReport pad;
        pad.element = "tee_1"; pad.identity = "src_%u#0"; pad.templateName = "src_%u"; pad.isSrc = true; pad.temporaryEnd = true;
        r.pads.push_back(pad);
        r.refused.push_back("a.src -> b.sink: no");
        const std::string json = gps::toJson(r);
        CHECK(json.find("\"template\":\"src_%u\"") != std::string::npos);
        CHECK(json.find("\"complete\":false") != std::string::npos);  // written for readers, computed by the struct
        gps::UpdateResult back;
        back.status.clear();
        CHECK(gps::fromJson(json, &back));
        CHECK_EQ(gps::toJson(back), json);
        CHECK(back.pad("tee_1", "src_%u#0", true) && !back.pad("tee_1", "src_%u#0", false));
        gps::UpdateResult none;
        none.status.clear();
        CHECK(gps::fromJson("{\"error\":\"x\"}", &none) && !none.valid());  // missing fields keep what the caller had
    }
    {
        gps::TelemetrySample s;
        s.elapsedMs = 250;
        gps::ElementTelemetry e;
        e.element = "queue_1"; e.queueMs = 33.5; e.inputBuffers = 8;
        s.elements.push_back(e);
        const std::string json = gps::toJson(s);
        CHECK(json.find("\"type\":\"sample\"") != std::string::npos && json.find("\"ageMs\":null") != std::string::npos);
        gps::TelemetrySample back;
        CHECK(gps::fromJson(json, &back));
        CHECK(back.elements.size() == 1 && back.elements[0].ageMs == -1 && back.elements[0].queueMs == 33.5);
        CHECK_EQ(gps::toJson(back), json);
        gps::RunResult end;
        CHECK(!gps::fromJson(gps::toJson(end), &back));  // gps-monitor's last line is a RunResult, not a sample
        CHECK(gps::fromJson(gps::toJson(end), &end) && end.status == "eos");
    }
    {
        gps::VersionInfo v;
        v.gstreamer.version = "1.20.3"; v.gstreamer.minor = 20; v.nvidia.present = true; v.plugins.count = 7;
        gps::VersionInfo back;
        CHECK(gps::fromJson(gps::toJson(v), &back));
        CHECK(back.gstreamer.version == "1.20.3" && back.gstreamer.minor == 20 && back.nvidia.present && back.plugins.count == 7);
        gps::FactoryList list{{gps::FactoryEntry{}}};
        list.elements[0].factory = "fakesink";
        gps::FactoryList listBack;
        CHECK(gps::fromJson(gps::toJson(list), &listBack) && listBack.elements.size() == 1 && listBack.elements[0].factory == "fakesink");
        gps::ErrorReply error;
        CHECK(gps::fromJson(gps::toJson(gps::ErrorReply{"bad \"caps\""}), &error) && error.error == "bad \"caps\"");
    }
    return finish("json_test");
}
