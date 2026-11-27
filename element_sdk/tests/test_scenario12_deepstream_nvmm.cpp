#include "gps_elem/archetypes.h"
#include "gps_elem/deepstream.h"
#include "gps_elem/register.h"
#include "test_harness.h"

#include <cstring>
#include <string>

namespace {

class NvmmPassthroughFilter : public gps::elem::TransformElement {
public:
    explicit NvmmPassthroughFilter(GstElement* raw) : TransformElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata("NVMM Passthrough Filter", "Filter/Video", "Preserves memory:NVMM CapsFeatures", "GstPipeStudio");
        gps::elem::VideoRawSpec spec;
        spec.formats = {"NV12", "RGBA"};
        spec.features = {"memory:NVMM"};
        addDefaultPorts(b, gps::elem::Caps::videoRaw(spec), gps::elem::Caps::videoRaw(spec));
    }

    GstFlowReturn transform(gps::elem::Buffer& buf, gps::elem::SrcPort& src) override {
        return src.push(std::move(buf));
    }
};

void testCapsFeaturesAndStructuredCaps() {
    auto nvmmCaps = gps::elem::Caps::withFeatures("video/x-raw", {"memory:NVMM"});
    TEST_CHECK(nvmmCaps.isValid());
    TEST_CHECK(nvmmCaps.hasFeature("memory:NVMM"));
    TEST_CHECK(!nvmmCaps.hasFeature("memory:DMABuf"));

    auto codecBuf = gps::elem::Buffer::fromPod<uint32_t>(0x12345678u);
    nvmmCaps.setInt("width", 1920).setInt("height", 1080).setString("format", "NV12").setCodecData(codecBuf);

    TEST_EQ(nvmmCaps.optInt("width").value_or(0), 1920);
    TEST_EQ(nvmmCaps.optInt("height").value_or(0), 1080);
    TEST_EQ(nvmmCaps.optString("format").value_or(""), std::string("NV12"));
    auto cd = nvmmCaps.codecData();
    TEST_CHECK(cd.has_value());
    TEST_EQ(cd->asPod<uint32_t>().value_or(0), 0x12345678u);

    // Verify querySinkCaps / querySrcCaps preserve memory:NVMM across NvmmPassthroughFilter
    GstElement* filter = gst_element_factory_make("test_nvmm_filter", nullptr);
    TEST_CHECK(filter != nullptr);
    GstPad* sinkPad = gst_element_get_static_pad(filter, "sink");
    GstPad* srcPad = gst_element_get_static_pad(filter, "src");

    GstCaps* queriedSink = gst_pad_query_caps(sinkPad, nullptr);
    TEST_CHECK(queriedSink != nullptr);
    auto wrappedSink = gps::elem::Caps::adopt(queriedSink);
    TEST_CHECK(wrappedSink.hasFeature("memory:NVMM"));

    GstCaps* queriedSrc = gst_pad_query_caps(srcPad, nullptr);
    TEST_CHECK(queriedSrc != nullptr);
    auto wrappedSrc = gps::elem::Caps::adopt(queriedSrc);
    TEST_CHECK(wrappedSrc.hasFeature("memory:NVMM"));

    gst_object_unref(sinkPad);
    gst_object_unref(srcPad);
    gst_object_unref(filter);
}

#ifdef GPS_ELEM_WITH_DEEPSTREAM
void testDeepStreamNvSurfaceAndBatchMeta() {
    // Plain buffer has no NvBatchMeta
    gps::elem::Buffer plain = gps::elem::Buffer::allocate(64);
    TEST_CHECK(!gps::elem::NvBatchMeta::fromBuffer(plain).has_value());

    // Synthetic NvBufSurface layout inspection via NvSurfaceBatch
    NvBufSurfaceParams params{};
    params.width = 1280;
    params.height = 720;
    params.pitch = 1280;
    params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
    params.dataPtr = reinterpret_cast<void*>(0xDEADBEEF);

    gps::elem::Buffer surfBuf = gps::elem::Buffer::allocate(sizeof(NvBufSurface));
    {
        auto map = surfBuf.write();
        TEST_CHECK(map.valid());
        auto* surf = reinterpret_cast<NvBufSurface*>(map.data());
        std::memset(surf, 0, sizeof(NvBufSurface));
        surf->gpuId = 0;
        surf->batchSize = 1;
        surf->numFilled = 1;
        surf->memType = NVBUF_MEM_CUDA_DEVICE;
        surf->surfaceList = &params;
    }

    auto batch = gps::elem::NvSurfaceBatch::mapRead(surfBuf);
    TEST_CHECK(batch.has_value());
    TEST_EQ(batch->batchSize(), 1u);
    TEST_EQ(batch->numFilled(), 1u);
    TEST_EQ(batch->gpuId(), 0u);
    TEST_EQ(batch->memType(), NVBUF_MEM_CUDA_DEVICE);
    TEST_EQ(batch->width(0), 1280u);
    TEST_EQ(batch->height(0), 720u);
    TEST_EQ(batch->pitch(0), 1280u);
    TEST_EQ(batch->colorFormat(0), NVBUF_COLOR_FORMAT_NV12);
    TEST_EQ(batch->dataPtr(0), reinterpret_cast<void*>(0xDEADBEEF));

    // Attach real NvDsBatchMeta using DeepStream API
    NvDsBatchMeta* dsBatch = nvds_create_batch_meta(2);
    TEST_CHECK(dsBatch != nullptr);
    NvDsFrameMeta* dsFrame = nvds_acquire_frame_meta_from_pool(dsBatch);
    TEST_CHECK(dsFrame != nullptr);
    NvDsObjectMeta* dsObj = nvds_acquire_obj_meta_from_pool(dsBatch);
    TEST_CHECK(dsObj != nullptr);
    nvds_add_obj_meta_to_frame(dsFrame, dsObj, nullptr);
    nvds_add_frame_meta_to_batch(dsBatch, dsFrame);

    NvDsMeta* gstMeta = gst_buffer_add_nvds_meta(
        surfBuf.get(),
        dsBatch,
        nullptr,
        [](gpointer data, gpointer /*user_data*/) -> gpointer {
            return data;
        },
        [](gpointer data, gpointer /*user_data*/) {
            nvds_destroy_batch_meta(static_cast<NvDsBatchMeta*>(data));
        });
    TEST_CHECK(gstMeta != nullptr);
    gstMeta->meta_type = NVDS_BATCH_GST_META;

    auto metaWrap = gps::elem::NvBatchMeta::fromBuffer(surfBuf);
    TEST_CHECK(metaWrap.has_value());
    TEST_EQ(metaWrap->numFrames(), 1u);
    TEST_EQ(metaWrap->objectCount(0), 1u);
    TEST_EQ(metaWrap->frames().size(), static_cast<size_t>(1));
    TEST_EQ(metaWrap->objects(metaWrap->frames()[0]).size(), static_cast<size_t>(1));
}
#endif

}  // namespace

int main(int argc, char** argv) {
    initGst(&argc, &argv);
    TEST_CHECK(gps::elem::registerElementStatic<NvmmPassthroughFilter>("test_nvmm_filter"));

    testCapsFeaturesAndStructuredCaps();
#ifdef GPS_ELEM_WITH_DEEPSTREAM
    testDeepStreamNvSurfaceAndBatchMeta();
#endif
    return 0;
}
