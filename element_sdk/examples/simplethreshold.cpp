#include "gps_elem/archetypes.h"
#include "gps_elem/register.h"

#include <algorithm>
#include <cstdint>

namespace {

class SimpleThreshold : public gps::elem::VideoFilterElement {
public:
    explicit SimpleThreshold(GstElement* raw) : VideoFilterElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata(
            "Simple Threshold Filter",
            "Filter/Effect/Video",
            "Applies binary threshold to GRAY8/RGBA video frames",
            "GstPipeStudio");

        gps::elem::VideoRawSpec spec;
        spec.formats = {"GRAY8", "RGBA"};
        addDefaultPorts(b, spec, spec);

        b.properties.prop("threshold", &SimpleThreshold::m_threshold, 128, 0, 255)
            .nick("Threshold")
            .blurb("Pixel intensity threshold (0-255)")
            .mutability(gps::elem::Mutability::Playing)
            .controllable(true);
    }

    GstFlowReturn filterInPlace(gps::elem::VideoFrame& frame) override {
        syncProperties(GST_CLOCK_TIME_NONE);
        const uint8_t thresh = static_cast<uint8_t>(std::clamp(m_threshold, 0, 255));
        const int h = frame.height();
        const int w = frame.width();
        const int pstride = frame.pixelStride(0);

        for (int y = 0; y < h; ++y) {
            uint8_t* row = frame.row(0, y);
            if (!row) {
                continue;
            }
            if (pstride == 1) {
                for (int x = 0; x < w; ++x) {
                    row[x] = (row[x] >= thresh) ? 255 : 0;
                }
            } else {
                for (int x = 0; x < w; ++x) {
                    uint8_t* px = row + x * pstride;
                    const uint8_t luma = static_cast<uint8_t>((px[0] + px[1] + px[2]) / 3);
                    const uint8_t out = (luma >= thresh) ? 255 : 0;
                    px[0] = out;
                    px[1] = out;
                    px[2] = out;
                }
            }
        }
        return GST_FLOW_OK;
    }

private:
    int32_t m_threshold = 128;
};

}  // namespace

GPS_ELEM_DEFINE_PLUGIN(
    simplethreshold,
    "Simple threshold video filter built on gps_element_sdk",
    "1.0.0",
    "LGPL",
    "gps_element_sdk",
    "https://github.com/GstPipeStudio",
    {
        return gps::elem::registerElement<SimpleThreshold>(plugin, "simplethreshold", GST_RANK_NONE) ? TRUE : FALSE;
    })
