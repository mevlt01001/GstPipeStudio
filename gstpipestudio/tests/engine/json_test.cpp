// json_test – the JSON writer: escaping, nesting, commas, numbers. No GStreamer needed.
#include "check.h"
#include "json.h"

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
    CHECK_EQ(jsonEscape("ğüşçö İ 日本 😀"), "ğüşçö İ 日本 😀");  // valid UTF-8 passes through untouched
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
    return finish("json_test");
}
