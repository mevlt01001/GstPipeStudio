#include "gps_elem/archetypes.h"
#include "gps_elem/register.h"
#include "test_harness.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace {

// In-place audio gain filter supporting both interleaved S16LE and planar F32LE
class AudioGainFilter : public gps::elem::AudioFilterElement {
public:
    explicit AudioGainFilter(GstElement* raw) : AudioFilterElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Audio Gain Filter", "Filter/Effect/Audio", "Scales raw audio samples", "GstPipeStudio");
        gps::elem::AudioRawSpec spec;
        spec.formats = {"S16LE", "F32LE"};
        addDefaultPorts(b, spec, spec);
    }

    GstFlowReturn filterInPlace(gps::elem::AudioFrame& frame) override {
        const int nSamples = frame.samples();
        if (m_inInfo->format() == GST_AUDIO_FORMAT_S16LE) {
            int16_t* data = frame.samplesAs<int16_t>(0);
            const int total = nSamples * frame.channels();
            for (int i = 0; i < total; ++i) {
                data[i] = static_cast<int16_t>(data[i] * 2);
            }
        } else if (m_inInfo->format() == GST_AUDIO_FORMAT_F32LE) {
            for (int p = 0; p < frame.planes(); ++p) {
                float* data = frame.samplesAs<float>(p);
                const int count = frame.isPlanar() ? nSamples : nSamples * frame.channels();
                for (int i = 0; i < count; ++i) {
                    data[i] *= 0.5f;
                }
            }
        }
        return GST_FLOW_OK;
    }
};

class AudioCollectorSink : public gps::elem::SinkElement {
public:
    explicit AudioCollectorSink(GstElement* raw) : SinkElement(raw) {
        clockSync().setEnabled(false);
    }

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Audio Collector Sink", "Sink/Audio", "Collects raw audio buffers", "GstPipeStudio");
        addDefaultPorts(b, gps::elem::Caps::audioRaw());
    }

    bool onSinkCaps(gps::elem::SinkPort& /*port*/, const gps::elem::Caps& caps) override {
        m_info = gps::elem::AudioInfo::fromCaps(caps);
        return m_info.has_value();
    }

    GstFlowReturn consume(gps::elem::Buffer& buf) override {
        TEST_CHECK(m_info.has_value());
        auto frame = gps::elem::AudioFrame::mapRead(*m_info, buf);
        TEST_CHECK(frame.has_value());
        const auto& cframe = *frame;
        if (m_info->format() == GST_AUDIO_FORMAT_S16LE) {
            const int16_t* ptr = cframe.samplesAs<int16_t>(0);
            TEST_CHECK(ptr != nullptr);
            for (int i = 0; i < cframe.samples() * cframe.channels(); ++i) {
                s16Samples.push_back(ptr[i]);
            }
        } else if (m_info->format() == GST_AUDIO_FORMAT_F32LE) {
            for (int p = 0; p < cframe.planes(); ++p) {
                const float* ptr = cframe.samplesAs<float>(p);
                TEST_CHECK(ptr != nullptr);
                const int count = cframe.isPlanar() ? cframe.samples() : cframe.samples() * cframe.channels();
                for (int i = 0; i < count; ++i) {
                    f32Samples.push_back(ptr[i]);
                }
            }
        }
        ++bufferCount;
        return GST_FLOW_OK;
    }

    std::optional<gps::elem::AudioInfo> m_info;
    std::vector<int16_t> s16Samples;
    std::vector<float> f32Samples;
    int bufferCount = 0;
};

void testInterleavedS16WithTeeCow() {
    auto* pipeline = gst_pipeline_new("audio_s16_cow");
    auto* src = gst_element_factory_make("audiotestsrc", nullptr);
    auto* capsfilter = gst_element_factory_make("capsfilter", nullptr);
    auto* tee = gst_element_factory_make("tee", nullptr);
    auto* q1 = gst_element_factory_make("queue", nullptr);
    auto* q2 = gst_element_factory_make("queue", nullptr);
    auto* gain = gst_element_factory_make("test_audio_gain", nullptr);
    auto* sinkFiltered = gst_element_factory_make("test_audio_sink", nullptr);
    auto* sinkRaw = gst_element_factory_make("test_audio_sink", nullptr);
    TEST_CHECK(src && capsfilter && tee && q1 && q2 && gain && sinkFiltered && sinkRaw);

    g_object_set(src, "wave", 4 /* silence? no, ticks/sine */, "num-buffers", 3, "samplesperbuffer", 160, nullptr);
    // Use constant-ish non-zero wave=0 (sine)
    g_object_set(src, "wave", 0, nullptr);
    auto caps = gps::elem::Caps::fromString("audio/x-raw,format=S16LE,layout=interleaved,rate=16000,channels=2");
    g_object_set(capsfilter, "caps", caps.get(), nullptr);

    gst_bin_add_many(GST_BIN(pipeline), src, capsfilter, tee, q1, q2, gain, sinkFiltered, sinkRaw, nullptr);
    TEST_CHECK(gst_element_link_many(src, capsfilter, tee, nullptr));
    TEST_CHECK(gst_element_link_many(q1, gain, sinkFiltered, nullptr));
    TEST_CHECK(gst_element_link_many(q2, sinkRaw, nullptr));

    GstPad* t1 = gst_element_request_pad_simple(tee, "src_%u");
    GstPad* t2 = gst_element_request_pad_simple(tee, "src_%u");
    GstPad* q1Sink = gst_element_get_static_pad(q1, "sink");
    GstPad* q2Sink = gst_element_get_static_pad(q2, "sink");
    TEST_EQ(gst_pad_link(t1, q1Sink), GST_PAD_LINK_OK);
    TEST_EQ(gst_pad_link(t2, q2Sink), GST_PAD_LINK_OK);
    gst_object_unref(q1Sink);
    gst_object_unref(q2Sink);
    gst_object_unref(t1);
    gst_object_unref(t2);

    TEST_CHECK(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    TEST_EQ(waitForBusTerminal(pipeline), GST_MESSAGE_EOS);

    auto* fImpl = static_cast<AudioCollectorSink*>(reinterpret_cast<gps::elem::GpsElementInstance*>(sinkFiltered)->impl);
    auto* rImpl = static_cast<AudioCollectorSink*>(reinterpret_cast<gps::elem::GpsElementInstance*>(sinkRaw)->impl);
    TEST_EQ(fImpl->bufferCount, 3);
    TEST_EQ(rImpl->bufferCount, 3);
    TEST_EQ(fImpl->s16Samples.size(), rImpl->s16Samples.size());
    TEST_CHECK(!fImpl->s16Samples.empty());

    // Verify COW preserved raw branch while gain branch doubled each sample
    bool foundNonZero = false;
    for (size_t i = 0; i < rImpl->s16Samples.size(); ++i) {
        if (rImpl->s16Samples[i] != 0) {
            foundNonZero = true;
        }
        TEST_EQ(fImpl->s16Samples[i], static_cast<int16_t>(rImpl->s16Samples[i] * 2));
    }
    TEST_CHECK(foundNonZero);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

void testPlanarF32() {
    auto* pipeline = gst_pipeline_new("audio_f32_planar");
    auto* src = gst_element_factory_make("audiotestsrc", nullptr);
    auto* conv = gst_element_factory_make("audioconvert", nullptr);
    auto* capsfilter = gst_element_factory_make("capsfilter", nullptr);
    auto* gain = gst_element_factory_make("test_audio_gain", nullptr);
    auto* sink = gst_element_factory_make("test_audio_sink", nullptr);
    TEST_CHECK(src && conv && capsfilter && gain && sink);

    g_object_set(src, "wave", 0, "num-buffers", 2, "samplesperbuffer", 128, nullptr);
    auto caps = gps::elem::Caps::fromString("audio/x-raw,format=F32LE,layout=non-interleaved,rate=48000,channels=2");
    g_object_set(capsfilter, "caps", caps.get(), nullptr);

    gst_bin_add_many(GST_BIN(pipeline), src, conv, capsfilter, gain, sink, nullptr);
    TEST_CHECK(gst_element_link_many(src, conv, capsfilter, gain, sink, nullptr));

    TEST_CHECK(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    TEST_EQ(waitForBusTerminal(pipeline), GST_MESSAGE_EOS);

    auto* sImpl = static_cast<AudioCollectorSink*>(reinterpret_cast<gps::elem::GpsElementInstance*>(sink)->impl);
    TEST_EQ(sImpl->bufferCount, 2);
    TEST_CHECK(sImpl->m_info.has_value());
    TEST_CHECK(sImpl->m_info->isPlanar());
    TEST_EQ(sImpl->f32Samples.size(), static_cast<size_t>(2 * 128 * 2));

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

}  // namespace

int main(int argc, char** argv) {
    initGst(&argc, &argv);
    TEST_CHECK(gps::elem::registerElementStatic<AudioGainFilter>("test_audio_gain"));
    TEST_CHECK(gps::elem::registerElementStatic<AudioCollectorSink>("test_audio_sink"));

    testInterleavedS16WithTeeCow();
    testPlanarF32();
    return 0;
}
