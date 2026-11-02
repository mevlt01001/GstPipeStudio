// deepstream_caps_test – the caps text the GUI writes (CapsChoice, with memory features) is what the s5 plan holds,
// and it survives the .conf round trip. The real GStreamer side of the same caps is checked in deepstream_test.py.
#include <string>

#include "capstext.h"
#include "check.h"
#include "plan.h"

using gps::CapsChoice;
using gps::CapsField;

static std::string capsOfLink(const gps::Plan& plan, const std::string& src, const std::string& sink) {
    for (const gps::PlanLink& l : plan.links) {
        if (l.srcElement == src && l.sinkElement == sink) return l.caps;
    }
    return "<no such link>";
}

int main() {
    gps::Plan plan;
    std::string error;
    bool loaded = gps::Plan::load(std::string(GPS_DS_PLANS) + "/s5_caps_choice.conf", &plan, &error);
    CHECK(loaded);
    if (!loaded) {
        std::printf("load: %s\n", error.c_str());
        return finish("deepstream_caps_test");
    }

    CapsChoice raw;
    raw.media = "video/x-raw";
    raw.fields = {{"format", "gchararray", "RGBA"}, {"width", "gint", "320"}, {"height", "gint", "240"}, {"framerate", "GstFraction", "15/1"}};
    CHECK_EQ(raw.toText(), capsOfLink(plan, "videotestsrc_1", "nvvideoconvert_1"));

    CapsChoice nvmm;
    nvmm.media = "video/x-raw";
    nvmm.features = {"memory:NVMM"};
    nvmm.fields = {{"format", "gchararray", "NV12"}, {"width", "gint", "160"}, {"height", "gint", "120"}};
    CHECK_EQ(nvmm.toText(), capsOfLink(plan, "nvvideoconvert_1", "nvvideoconvert_2"));

    CapsChoice back;
    back.media = "video/x-raw";
    back.fields = {{"format", "gchararray", "I420"}, {"width", "gint", "80"}, {"height", "gint", "60"}};
    CHECK_EQ(back.toText(), capsOfLink(plan, "nvvideoconvert_2", "fakesink_1"));

    // features alone, and the feature in front of nothing else
    CapsChoice onlyFeature;
    onlyFeature.media = "video/x-raw";
    onlyFeature.features = {"memory:NVMM"};
    CHECK_EQ(onlyFeature.toText(), std::string("video/x-raw(memory:NVMM)"));

    // .conf round trip keeps the parentheses and the feature
    gps::Plan again;
    CHECK(gps::Plan::fromConf(plan.toConf(), &again, &error));
    CHECK_EQ(capsOfLink(again, "nvvideoconvert_1", "nvvideoconvert_2"), nvmm.toText());

    // setCaps writes the same text
    gps::Plan edited = plan;
    CHECK(edited.setCaps("nvvideoconvert_1", "src", onlyFeature.toText()));
    CHECK_EQ(capsOfLink(edited, "nvvideoconvert_1", "nvvideoconvert_2"), std::string("video/x-raw(memory:NVMM)"));

    return finish("deepstream_caps_test");
}
