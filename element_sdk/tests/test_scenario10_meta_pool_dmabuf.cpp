#include "gps_elem/allocation.h"
#include "gps_elem/archetypes.h"
#include "gps_elem/meta.h"
#include "gps_elem/register.h"
#include "test_harness.h"

#include <sys/mman.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

struct DetectionBox {
    float x = 0.0f;
    float y = 0.0f;
    std::string label;
};

struct DetectionReport {
    std::string sourceTag;
    std::vector<DetectionBox> boxes;
};

class MetaAttachingSource : public gps::elem::SourceElement {
public:
    explicit MetaAttachingSource(GstElement* raw) : SourceElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Meta Attaching Source", "Source/Video", "Attaches CustomMeta and uses BufferPool", "GstPipeStudio");
        gps::elem::VideoRawSpec spec;
        spec.formats = {"GRAY8"};
        spec.width = 16;
        spec.height = 16;
        spec.fpsNum = 30;
        spec.fpsDen = 1;
        addDefaultPorts(b, gps::elem::Caps::videoRaw(spec));
    }

    bool start() override {
        m_count = 0;
        if (auto* src = srcPort("src")) {
            gps::elem::VideoRawSpec spec;
            spec.formats = {"GRAY8"};
            spec.width = 16;
            spec.height = 16;
            spec.fpsNum = 30;
            spec.fpsDen = 1;
            src->setCaps(gps::elem::Caps::videoRaw(spec));
            TEST_CHECK(src->negotiateAllocation());
            TEST_CHECK(src->pool().isValid());
        }
        return SourceElement::start();
    }

    GstFlowReturn produce(gps::elem::SrcPort& src) override {
        if (m_count >= 2) {
            return GST_FLOW_EOS;
        }
        gps::elem::Buffer buf = src.acquireBuffer(16 * 16);
        TEST_CHECK(buf.isValid());
        {
            auto map = buf.write();
            TEST_CHECK(map.valid());
            std::memset(map.data(), 42, map.size());
        }
        DetectionReport report;
        report.sourceTag = "cam_" + std::to_string(m_count);
        report.boxes.push_back(DetectionBox{0.1f, 0.2f, "person"});
        report.boxes.push_back(DetectionBox{0.5f, 0.6f, "car"});
        auto* attached = gps::elem::CustomMeta<DetectionReport>::attach(buf, std::move(report));
        TEST_CHECK(attached != nullptr);

        buf.setPts(m_count * 33 * GST_MSECOND);
        buf.setDuration(33 * GST_MSECOND);
        ++m_count;
        return src.push(std::move(buf));
    }

private:
    int m_count = 0;
};

// Out-of-place transform that mutates a copy of DetectionReport (testing COW + copyMetadataFrom)
class MetaMutatingFilter : public gps::elem::TransformElement {
public:
    explicit MetaMutatingFilter(GstElement* raw) : TransformElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Meta Mutating Filter", "Filter/Video", "Out-of-place filter copying CustomMeta", "GstPipeStudio");
        addDefaultPorts(b);
    }

    bool proposeAllocation(gps::elem::SinkPort& /*port*/, GstQuery* query) override {
        auto pool = gps::elem::BufferPool::createSystemPool();
        auto caps = gps::elem::Caps::fromString("video/x-raw,format=GRAY8,width=16,height=16,framerate=30/1");
        TEST_CHECK(pool.configure(caps, 16 * 16, 2, 4));
        gst_query_add_allocation_pool(query, pool.raw(), 16 * 16, 2, 4);
        proposedPool = true;
        return true;
    }

    GstFlowReturn transform(gps::elem::Buffer& buf, gps::elem::SrcPort& src) override {
        gps::elem::Buffer out = gps::elem::Buffer::allocate(buf.size());
        out.copyMetadataFrom(buf);
        if (auto* rep = gps::elem::CustomMeta<DetectionReport>::getMutable(out)) {
            rep->sourceTag += "_mutated";
            rep->boxes.push_back(DetectionBox{0.9f, 0.9f, "truck"});
        }
        return src.push(std::move(out));
    }

    bool proposedPool = false;
};

class MetaCollectorSink : public gps::elem::SinkElement {
public:
    explicit MetaCollectorSink(GstElement* raw) : SinkElement(raw) {
        clockSync().setEnabled(false);
    }

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("Meta Collector Sink", "Sink/Video", "Collects DetectionReport metas", "GstPipeStudio");
        addDefaultPorts(b);
    }

    GstFlowReturn consume(gps::elem::Buffer& buf) override {
        const auto* rep = gps::elem::CustomMeta<DetectionReport>::get(buf);
        TEST_CHECK(rep != nullptr);
        reports.push_back(*rep);
        return GST_FLOW_OK;
    }

    std::vector<DetectionReport> reports;
};

void testCustomMetaAndPoolAllocation() {
    auto* pipeline = gst_pipeline_new("meta_pool_test");
    auto* src = gst_element_factory_make("test_meta_src", nullptr);
    auto* tee = gst_element_factory_make("tee", nullptr);
    auto* q1 = gst_element_factory_make("queue", nullptr);
    auto* q2 = gst_element_factory_make("queue", nullptr);
    auto* filter = gst_element_factory_make("test_meta_filter", nullptr);
    auto* sinkMut = gst_element_factory_make("test_meta_sink", nullptr);
    auto* sinkOrig = gst_element_factory_make("test_meta_sink", nullptr);
    TEST_CHECK(src && tee && q1 && q2 && filter && sinkMut && sinkOrig);

    gst_bin_add_many(GST_BIN(pipeline), src, tee, q1, q2, filter, sinkMut, sinkOrig, nullptr);
    TEST_CHECK(gst_element_link(src, tee));
    TEST_CHECK(gst_element_link_many(q1, filter, sinkMut, nullptr));
    TEST_CHECK(gst_element_link(q2, sinkOrig));

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

    auto* fImpl = static_cast<MetaMutatingFilter*>(reinterpret_cast<gps::elem::GpsElementInstance*>(filter)->impl);
    TEST_CHECK(fImpl->proposedPool);

    auto* mSink = static_cast<MetaCollectorSink*>(reinterpret_cast<gps::elem::GpsElementInstance*>(sinkMut)->impl);
    auto* oSink = static_cast<MetaCollectorSink*>(reinterpret_cast<gps::elem::GpsElementInstance*>(sinkOrig)->impl);
    TEST_EQ(mSink->reports.size(), static_cast<size_t>(2));
    TEST_EQ(oSink->reports.size(), static_cast<size_t>(2));

    TEST_EQ(oSink->reports[0].sourceTag, std::string("cam_0"));
    TEST_EQ(oSink->reports[0].boxes.size(), static_cast<size_t>(2));
    TEST_EQ(mSink->reports[0].sourceTag, std::string("cam_0_mutated"));
    TEST_EQ(mSink->reports[0].boxes.size(), static_cast<size_t>(3));
    TEST_EQ(mSink->reports[0].boxes[2].label, std::string("truck"));

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

void testDmaBufMemfd() {
    int fd = memfd_create("gps_dmabuf_test", MFD_CLOEXEC);
    TEST_CHECK(fd >= 0);
    const size_t sz = 4096;
    TEST_EQ(ftruncate(fd, static_cast<off_t>(sz)), 0);

    auto alloc = gps::elem::DmaBufAllocator::create();
    TEST_CHECK(alloc.isValid());
    gps::elem::Buffer buf = alloc.alloc(fd, sz, true);
    TEST_CHECK(buf.isValid());
    TEST_CHECK(gps::elem::DmaBufAllocator::isDmaBuf(buf));
    TEST_EQ(gps::elem::DmaBufAllocator::fd(buf), fd);

    {
        auto wmap = buf.write();
        TEST_CHECK(wmap.valid());
        TEST_EQ(wmap.size(), sz);
        wmap.data()[0] = 0xAB;
        wmap.data()[4095] = 0xCD;
    }
    {
        auto rmap = buf.read();
        TEST_CHECK(rmap.valid());
        TEST_EQ(rmap.data()[0], 0xAB);
        TEST_EQ(rmap.data()[4095], 0xCD);
    }

    // Verify CustomMeta remove() works
    gps::elem::CustomMeta<DetectionReport>::attach(buf, DetectionReport{"dma", {}});
    TEST_CHECK(gps::elem::CustomMeta<DetectionReport>::get(buf) != nullptr);
    TEST_CHECK(gps::elem::CustomMeta<DetectionReport>::remove(buf));
    TEST_CHECK(gps::elem::CustomMeta<DetectionReport>::get(buf) == nullptr);
}

}  // namespace

int main(int argc, char** argv) {
    initGst(&argc, &argv);
    TEST_CHECK(gps::elem::registerElementStatic<MetaAttachingSource>("test_meta_src"));
    TEST_CHECK(gps::elem::registerElementStatic<MetaMutatingFilter>("test_meta_filter"));
    TEST_CHECK(gps::elem::registerElementStatic<MetaCollectorSink>("test_meta_sink"));

    testCustomMetaAndPoolAllocation();
    testDmaBufMemfd();
    return 0;
}
