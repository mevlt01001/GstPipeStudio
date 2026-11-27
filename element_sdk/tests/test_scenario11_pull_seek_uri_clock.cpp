#include "gps_elem/archetypes.h"
#include "gps_elem/driver.h"
#include "gps_elem/register.h"
#include "test_harness.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

// Pull-capable + seekable + clock-providing + URI-handling source
class SeekPullUriClockSrc : public gps::elem::Element {
public:
    explicit SeekPullUriClockSrc(GstElement* raw) : Element(raw) {
        setClockProvider(true);
        m_clock = gst_system_clock_obtain();
        for (size_t i = 0; i < sizeof(m_data); ++i) {
            m_data[i] = static_cast<uint8_t>(i & 0xFF);
        }
    }

    ~SeekPullUriClockSrc() override {
        if (m_clock) {
            gst_object_unref(m_clock);
        }
    }

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Seek Pull URI Clock Src", "Source/Generic", "Tests pull/seek/uri/clock", "GstPipeStudio");
        b.addSrcPort("src", gps::elem::Caps::any(), gps::elem::Presence::Always);
        b.enableUriHandler(GST_URI_SRC, {"gpsmem"});
    }

    bool open() override {
        if (auto* src = srcPort("src")) {
            src->setGetRangeCallback([this](uint64_t offset, uint32_t length, gps::elem::Buffer& outBuf) {
                if (offset >= sizeof(m_data)) {
                    return GST_FLOW_EOS;
                }
                const size_t avail = sizeof(m_data) - static_cast<size_t>(offset);
                const size_t toRead = length < avail ? length : avail;
                outBuf = gps::elem::Buffer::allocate(toRead);
                auto map = outBuf.write();
                std::memcpy(map.data(), m_data + offset, toRead);
                m_position = static_cast<gint64>(offset + toRead);
                return GST_FLOW_OK;
            });
        }
        return true;
    }

    std::string currentUri() const override { return m_uri; }

    bool setUri(const std::string& uri, std::string& errorOut) override {
        if (uri.rfind("gpsmem://", 0) != 0) {
            errorOut = "Expected gpsmem://";
            return false;
        }
        m_uri = uri;
        return true;
    }

    GstClock* provideClock() override {
        return m_clock ? GST_CLOCK_CAST(gst_object_ref(m_clock)) : nullptr;
    }

    bool onSeek(
        gps::elem::SrcPort& /*port*/,
        double /*rate*/,
        GstFormat format,
        GstSeekFlags /*flags*/,
        GstSeekType /*startType*/,
        gint64 start,
        GstSeekType /*stopType*/,
        gint64 /*stop*/) override {
        if (format != GST_FORMAT_BYTES && format != GST_FORMAT_TIME) {
            return false;
        }
        m_seekCount++;
        m_position = start;
        return true;
    }

    bool queryDuration(gps::elem::SrcPort& /*port*/, GstFormat format, gint64& durationOut) override {
        if (format == GST_FORMAT_BYTES) {
            durationOut = static_cast<gint64>(sizeof(m_data));
            return true;
        }
        if (format == GST_FORMAT_TIME) {
            durationOut = 10 * GST_SECOND;
            return true;
        }
        return false;
    }

    bool queryPosition(gps::elem::SrcPort& /*port*/, GstFormat format, gint64& positionOut) override {
        if (format == GST_FORMAT_BYTES || format == GST_FORMAT_TIME) {
            positionOut = m_position;
            return true;
        }
        return false;
    }

    bool querySeeking(
        gps::elem::SrcPort& /*port*/,
        GstFormat format,
        gboolean& seekableOut,
        gint64& segmentStartOut,
        gint64& segmentEndOut) override {
        if (format == GST_FORMAT_BYTES || format == GST_FORMAT_TIME) {
            seekableOut = TRUE;
            segmentStartOut = 0;
            segmentEndOut = (format == GST_FORMAT_BYTES) ? static_cast<gint64>(sizeof(m_data)) : 10 * GST_SECOND;
            return true;
        }
        return false;
    }

    int seekCount() const noexcept { return m_seekCount; }

private:
    uint8_t m_data[256]{};
    std::string m_uri;
    GstClock* m_clock = nullptr;
    gint64 m_position = 0;
    int m_seekCount = 0;
};

// Pull-mode sink
class PullReaderSink : public gps::elem::Element {
public:
    explicit PullReaderSink(GstElement* raw) : Element(raw) {
        setSinkRole(true);
        setLive(true);
    }

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Pull Reader Sink", "Sink/Generic", "Pulls ranges in pull mode", "GstPipeStudio");
        b.addSinkPort("sink", gps::elem::Caps::any(), gps::elem::Presence::Always, gps::elem::InputMode::Direct);
    }

    bool open() override {
        if (auto* sink = sinkPort("sink")) {
            sink->setPullModePreferred(true);
        }
        return true;
    }

    bool play() override {
        auto* sink = sinkPort("sink");
        TEST_CHECK(sink != nullptr);
        TEST_CHECK(sink->isPullModeActive());
        return m_driver.start(this, [this, sink]() {
            if (m_offset >= 256) {
                postOrDeferEos();
                m_driver.pause();
                return;
            }
            gps::elem::Buffer buf;
            GstFlowReturn ret = sink->pullRange(m_offset, 64, buf);
            if (ret == GST_FLOW_EOS) {
                postOrDeferEos();
                m_driver.pause();
                return;
            }
            TEST_EQ(ret, GST_FLOW_OK);
            auto map = buf.read();
            TEST_CHECK(map.valid());
            pulledBytes.insert(pulledBytes.end(), map.data(), map.data() + map.size());
            m_offset += map.size();
        });
    }

    void unblock() override { m_driver.unblock(); }
    bool stop() override {
        m_driver.stop();
        return true;
    }

    std::vector<uint8_t> pulledBytes;

private:
    gps::elem::TaskDriver m_driver;
    uint64_t m_offset = 0;
};

// Sparse GAP test elements
class GapEmitterSrc : public gps::elem::SourceElement {
public:
    explicit GapEmitterSrc(GstElement* raw) : SourceElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Gap Emitter Src", "Source/Generic", "Emits GAP and Tag events", "GstPipeStudio");
        addDefaultPorts(b);
    }

    GstFlowReturn produce(gps::elem::SrcPort& src) override {
        if (m_done) {
            return GST_FLOW_EOS;
        }
        m_done = true;
        GstTagList* tags = gst_tag_list_new(GST_TAG_TITLE, "GapStream", nullptr);
        TEST_CHECK(src.pushTags(tags));
        TEST_CHECK(src.pushGap(500 * GST_MSECOND, 100 * GST_MSECOND));
        return GST_FLOW_OK;
    }

private:
    bool m_done = false;
};

class GapReceiverSink : public gps::elem::SinkElement {
public:
    explicit GapReceiverSink(GstElement* raw) : SinkElement(raw) {
        clockSync().setEnabled(false);
    }

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Gap Receiver Sink", "Sink/Generic", "Receives GAP events", "GstPipeStudio");
        addDefaultPorts(b);
    }

    bool onGap(gps::elem::SinkPort& /*port*/, GstClockTime pts, GstClockTime duration) override {
        gapReceived = true;
        gapPts = pts;
        gapDuration = duration;
        commitPreroll();
        return true;
    }

    GstFlowReturn consume(gps::elem::Buffer& /*buf*/) override {
        return GST_FLOW_OK;
    }

    bool gapReceived = false;
    GstClockTime gapPts = GST_CLOCK_TIME_NONE;
    GstClockTime gapDuration = GST_CLOCK_TIME_NONE;
};

void testPullSeekUriClock() {
    GError* err = nullptr;
    GstElement* src = gst_element_make_from_uri(GST_URI_SRC, "gpsmem://buffer0", "urisp", &err);
    TEST_CHECK(src != nullptr);
    TEST_CHECK(err == nullptr);

    gchar* uriOut = gst_uri_handler_get_uri(GST_URI_HANDLER(src));
    TEST_CHECK(uriOut != nullptr);
    TEST_EQ(std::string(uriOut), std::string("gpsmem://buffer0"));
    g_free(uriOut);

    GstClock* provided = gst_element_provide_clock(src);
    TEST_CHECK(provided != nullptr);
    gst_object_unref(provided);

    GstElement* sink = gst_element_factory_make("test_pull_sink", nullptr);
    TEST_CHECK(sink != nullptr);

    GstElement* pipeline = gst_pipeline_new("pull_seek_test");
    gst_bin_add_many(GST_BIN(pipeline), src, sink, nullptr);
    TEST_CHECK(gst_element_link(src, sink));

    TEST_CHECK(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    TEST_EQ(waitForBusTerminal(pipeline), GST_MESSAGE_EOS);

    auto* sinkImpl = static_cast<PullReaderSink*>(reinterpret_cast<gps::elem::GpsElementInstance*>(sink)->impl);
    TEST_EQ(sinkImpl->pulledBytes.size(), static_cast<size_t>(256));
    for (size_t i = 0; i < 256; ++i) {
        TEST_EQ(sinkImpl->pulledBytes[i], static_cast<uint8_t>(i & 0xFF));
    }

    // Query duration, position, seeking, and send SEEK on src pad
    GstPad* srcPad = gst_element_get_static_pad(src, "src");
    gint64 dur = 0;
    TEST_CHECK(gst_pad_query_duration(srcPad, GST_FORMAT_BYTES, &dur));
    TEST_EQ(dur, 256);

    gint64 pos = 0;
    TEST_CHECK(gst_pad_query_position(srcPad, GST_FORMAT_BYTES, &pos));
    TEST_EQ(pos, 256);

    GstQuery* sq = gst_query_new_seeking(GST_FORMAT_BYTES);
    TEST_CHECK(gst_pad_query(srcPad, sq));
    gboolean seekable = FALSE;
    gint64 sStart = 0, sEnd = 0;
    gst_query_parse_seeking(sq, nullptr, &seekable, &sStart, &sEnd);
    TEST_CHECK(seekable == TRUE);
    TEST_EQ(sStart, 0);
    TEST_EQ(sEnd, 256);
    gst_query_unref(sq);

    TEST_CHECK(gst_pad_send_event(
        srcPad,
        gst_event_new_seek(1.0, GST_FORMAT_BYTES, GST_SEEK_FLAG_FLUSH, GST_SEEK_TYPE_SET, 128, GST_SEEK_TYPE_NONE, 0)));
    auto* srcImpl = static_cast<SeekPullUriClockSrc*>(reinterpret_cast<gps::elem::GpsElementInstance*>(src)->impl);
    TEST_EQ(srcImpl->seekCount(), 1);
    TEST_CHECK(gst_pad_query_position(srcPad, GST_FORMAT_BYTES, &pos));
    TEST_EQ(pos, 128);

    gst_object_unref(srcPad);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

void testGapAndTags() {
    auto* pipeline = gst_pipeline_new("gap_test");
    auto* src = gst_element_factory_make("test_gap_src", nullptr);
    auto* sink = gst_element_factory_make("test_gap_sink", nullptr);
    TEST_CHECK(src && sink);

    gst_bin_add_many(GST_BIN(pipeline), src, sink, nullptr);
    TEST_CHECK(gst_element_link(src, sink));

    TEST_CHECK(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    TEST_EQ(waitForBusTerminal(pipeline), GST_MESSAGE_EOS);

    auto* sImpl = static_cast<GapReceiverSink*>(reinterpret_cast<gps::elem::GpsElementInstance*>(sink)->impl);
    TEST_CHECK(sImpl->gapReceived);
    TEST_EQ(sImpl->gapPts, static_cast<GstClockTime>(500 * GST_MSECOND));
    TEST_EQ(sImpl->gapDuration, static_cast<GstClockTime>(100 * GST_MSECOND));

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

}  // namespace

int main(int argc, char** argv) {
    initGst(&argc, &argv);
    TEST_CHECK(gps::elem::registerElementStatic<SeekPullUriClockSrc>("test_seek_pull_uri_src", GST_RANK_PRIMARY));
    TEST_CHECK(gps::elem::registerElementStatic<PullReaderSink>("test_pull_sink"));
    TEST_CHECK(gps::elem::registerElementStatic<GapEmitterSrc>("test_gap_src"));
    TEST_CHECK(gps::elem::registerElementStatic<GapReceiverSink>("test_gap_sink"));

    testPullSeekUriClock();
    testGapAndTags();
    return 0;
}
