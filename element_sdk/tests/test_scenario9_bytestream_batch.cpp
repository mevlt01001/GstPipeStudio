#include "gps_elem/archetypes.h"
#include "gps_elem/byte_stream.h"
#include "gps_elem/register.h"
#include "test_harness.h"

#include <cstring>
#include <vector>

namespace {

// Frames packets delimited by 0xAA 0x55 0xAA 0x55 + 2-byte length
class SyncPacketFramer : public gps::elem::TransformElement {
public:
    explicit SyncPacketFramer(GstElement* raw) : TransformElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Sync Packet Framer", "Codec/Parser", "Frames 0xAA55AA55 packets via ByteStreamAdapter", "GstPipeStudio");
        addDefaultPorts(b);
    }

    bool onSinkEvent(gps::elem::SinkPort& port, GstEvent* event) override {
        if (GST_EVENT_TYPE(event) == GST_EVENT_FLUSH_STOP) {
            m_adapter.clear();
        }
        return TransformElement::onSinkEvent(port, event);
    }

    GstFlowReturn transform(gps::elem::Buffer& buf, gps::elem::SrcPort& src) override {
        m_adapter.push(std::move(buf));

        while (m_adapter.available() >= 6) {
            gssize off = m_adapter.maskedScanUint32(0xFFFFFFFFu, 0xAA55AA55u, 0, m_adapter.available());
            if (off < 0) {
                const size_t keep = m_adapter.available() >= 3 ? 3 : m_adapter.available();
                m_adapter.flush(m_adapter.available() - keep);
                break;
            }
            if (off > 0) {
                m_adapter.flush(static_cast<size_t>(off));
            }
            if (m_adapter.available() < 6) {
                break;
            }
            auto hdr = m_adapter.copyBytes(4, 2);
            const size_t payloadLen = (static_cast<size_t>(hdr[0]) << 8) | hdr[1];
            if (m_adapter.available() < 6 + payloadLen) {
                break;
            }
            m_adapter.flush(6);
            GstClockTime pts = GST_CLOCK_TIME_NONE;
            gps::elem::Buffer pkt = m_adapter.takeBuffer(payloadLen, &pts, nullptr);
            pkt.setPts(pts);
            pkt.setDeltaUnit(false);
            GstFlowReturn ret = src.push(std::move(pkt));
            if (ret != GST_FLOW_OK) {
                return ret;
            }
        }
        return GST_FLOW_OK;
    }

    size_t adapterAvailable() const noexcept { return m_adapter.available(); }

private:
    gps::elem::ByteStreamAdapter m_adapter;
};

class PacketCollectorSink : public gps::elem::SinkElement {
public:
    explicit PacketCollectorSink(GstElement* raw) : SinkElement(raw) {
        clockSync().setEnabled(false);
    }

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Packet Collector Sink", "Sink/Generic", "Collects framed packets and BufferLists", "GstPipeStudio");
        addDefaultPorts(b);
    }

    bool open() override {
        if (auto* sink = sinkPort("sink")) {
            sink->setDirectListCallback([this](gps::elem::BufferList list) {
                ++listCallbacks;
                for (size_t i = 0; i < list.size(); ++i) {
                    gps::elem::Buffer b = list.get(i);
                    consume(b);
                }
                return GST_FLOW_OK;
            });
        }
        return SinkElement::open();
    }

    GstFlowReturn consume(gps::elem::Buffer& buf) override {
        auto map = buf.read();
        TEST_CHECK(map.valid());
        packets.emplace_back(map.data(), map.data() + map.size());
        ptsList.push_back(buf.pts());
        deltaFlags.push_back(buf.isDeltaUnit());
        return GST_FLOW_OK;
    }

    std::vector<std::vector<uint8_t>> packets;
    std::vector<GstClockTime> ptsList;
    std::vector<bool> deltaFlags;
    int listCallbacks = 0;
};

void testByteStreamAdapterFraming() {
    auto* pipeline = gst_pipeline_new("bytestream_test");
    auto* framer = gst_element_factory_make("test_sync_framer", nullptr);
    auto* sink = gst_element_factory_make("test_pkt_sink", nullptr);
    TEST_CHECK(framer && sink);

    gst_bin_add_many(GST_BIN(pipeline), framer, sink, nullptr);
    TEST_CHECK(gst_element_link(framer, sink));
    TEST_CHECK(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);

    GstPad* framerSink = gst_element_get_static_pad(framer, "sink");
    gst_pad_send_event(framerSink, gst_event_new_stream_start("bs-stream"));
    GstSegment seg;
    gst_segment_init(&seg, GST_FORMAT_TIME);
    gst_pad_send_event(framerSink, gst_event_new_segment(&seg));

    // Chunk 1: garbage(2 bytes) + sync(4) + len(2)=4 + partial payload(2 bytes)
    const uint8_t chunk1[] = {0x99, 0x88, 0xAA, 0x55, 0xAA, 0x55, 0x00, 0x04, 0x11, 0x22};
    gps::elem::Buffer b1 = gps::elem::Buffer::allocate(sizeof(chunk1));
    {
        auto m = b1.write();
        std::memcpy(m.data(), chunk1, sizeof(chunk1));
    }
    b1.setPts(100 * GST_MSECOND);
    TEST_EQ(gst_pad_chain(framerSink, b1.release()), GST_FLOW_OK);

    // Chunk 2: rest of packet 1 (2 bytes) + full packet 2 (sync + len=2 + 0xDE, 0xAD)
    const uint8_t chunk2[] = {0x33, 0x44, 0xAA, 0x55, 0xAA, 0x55, 0x00, 0x02, 0xDE, 0xAD};
    gps::elem::Buffer b2 = gps::elem::Buffer::allocate(sizeof(chunk2));
    {
        auto m = b2.write();
        std::memcpy(m.data(), chunk2, sizeof(chunk2));
    }
    b2.setPts(200 * GST_MSECOND);
    TEST_EQ(gst_pad_chain(framerSink, b2.release()), GST_FLOW_OK);

    auto* sImpl = static_cast<PacketCollectorSink*>(reinterpret_cast<gps::elem::GpsElementInstance*>(sink)->impl);
    TEST_EQ(sImpl->packets.size(), static_cast<size_t>(2));
    TEST_EQ(sImpl->packets[0].size(), static_cast<size_t>(4));
    TEST_EQ(sImpl->packets[0][0], 0x11);
    TEST_EQ(sImpl->packets[0][3], 0x44);
    TEST_EQ(sImpl->ptsList[0], static_cast<GstClockTime>(100 * GST_MSECOND));
    TEST_EQ(sImpl->packets[1].size(), static_cast<size_t>(2));
    TEST_EQ(sImpl->packets[1][0], 0xDE);
    TEST_EQ(sImpl->packets[1][1], 0xAD);
    TEST_EQ(sImpl->ptsList[1], static_cast<GstClockTime>(200 * GST_MSECOND));

    // Push partial data then FLUSH_STOP -> adapter must clear
    const uint8_t partial[] = {0xAA, 0x55, 0xAA, 0x55, 0x00, 0x10};
    gps::elem::Buffer b3 = gps::elem::Buffer::allocate(sizeof(partial));
    {
        auto m = b3.write();
        std::memcpy(m.data(), partial, sizeof(partial));
    }
    TEST_EQ(gst_pad_chain(framerSink, b3.release()), GST_FLOW_OK);
    auto* fImpl = static_cast<SyncPacketFramer*>(reinterpret_cast<gps::elem::GpsElementInstance*>(framer)->impl);
    TEST_EQ(fImpl->adapterAvailable(), static_cast<size_t>(6));

    gst_pad_send_event(framerSink, gst_event_new_flush_start());
    gst_pad_send_event(framerSink, gst_event_new_flush_stop(TRUE));
    TEST_EQ(fImpl->adapterAvailable(), static_cast<size_t>(0));

    gst_object_unref(framerSink);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

class BatchListSource : public gps::elem::SourceElement {
public:
    explicit BatchListSource(GstElement* raw) : SourceElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Batch List Source", "Source/Generic", "Pushes BufferList batches", "GstPipeStudio");
        addDefaultPorts(b);
    }

    GstFlowReturn produce(gps::elem::SrcPort& src) override {
        if (m_sent) {
            return GST_FLOW_EOS;
        }
        m_sent = true;
        auto list = gps::elem::BufferList::createSized(3);
        for (int i = 0; i < 3; ++i) {
            auto b = gps::elem::Buffer::fromPod<uint32_t>(static_cast<uint32_t>(10 + i));
            b.setDeltaUnit(i > 0);
            b.setDiscont(i == 0);
            b.setHeader(i == 0);
            list.add(std::move(b));
        }
        return src.pushList(std::move(list));
    }

private:
    bool m_sent = false;
};

void testBufferListPushAndFallback() {
    auto* pipeline = gst_pipeline_new("batch_list_test");
    auto* src = gst_element_factory_make("test_batch_src", nullptr);
    auto* identity = gst_element_factory_make("identity", nullptr);
    auto* sink = gst_element_factory_make("test_pkt_sink", nullptr);
    TEST_CHECK(src && identity && sink);

    gst_bin_add_many(GST_BIN(pipeline), src, identity, sink, nullptr);
    TEST_CHECK(gst_element_link_many(src, identity, sink, nullptr));

    TEST_CHECK(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    TEST_EQ(waitForBusTerminal(pipeline), GST_MESSAGE_EOS);

    auto* sImpl = static_cast<PacketCollectorSink*>(reinterpret_cast<gps::elem::GpsElementInstance*>(sink)->impl);
    TEST_EQ(sImpl->packets.size(), static_cast<size_t>(3));
    TEST_EQ(sImpl->deltaFlags[0], false);
    TEST_EQ(sImpl->deltaFlags[1], true);
    TEST_EQ(sImpl->deltaFlags[2], true);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

}  // namespace

int main(int argc, char** argv) {
    initGst(&argc, &argv);
    TEST_CHECK(gps::elem::registerElementStatic<SyncPacketFramer>("test_sync_framer"));
    TEST_CHECK(gps::elem::registerElementStatic<PacketCollectorSink>("test_pkt_sink"));
    TEST_CHECK(gps::elem::registerElementStatic<BatchListSource>("test_batch_src"));

    testByteStreamAdapterFraming();
    testBufferListPushAndFallback();
    return 0;
}
