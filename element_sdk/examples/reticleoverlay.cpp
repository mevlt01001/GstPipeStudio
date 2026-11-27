#include "gps_elem/archetypes.h"
#include "gps_elem/register.h"

#include <algorithm>
#include <cstring>
#include <mutex>

namespace {

class ReticleOverlay : public gps::elem::Element {
public:
    explicit ReticleOverlay(GstElement* raw) : Element(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata(
            "Reticle Overlay",
            "Filter/Effect/Video",
            "2-to-1 video + non-blocking alpha mailbox overlay",
            "GstPipeStudio");

        gps::elem::VideoRawSpec vidSpec;
        vidSpec.formats = {"GRAY8", "RGBA"};
        b.addSinkPort("sink_video", gps::elem::Caps::videoRaw(vidSpec), gps::elem::Presence::Always, gps::elem::InputMode::BoundedQueue, 4, gps::elem::OverflowPolicy::Block);
        b.addSinkPort("sink_param", gps::elem::Caps::custom("application/x-float"), gps::elem::Presence::Always, gps::elem::InputMode::Mailbox);
        b.addSrcPort("src", gps::elem::Caps::videoRaw(vidSpec), gps::elem::Presence::Always);
    }

    bool open() override {
        auto* vid = sinkPort("sink_video");
        auto* param = sinkPort("sink_param");
        auto* src = srcPort("src");
        if (!vid || !param || !src) {
            return false;
        }
        linkInternal(*vid, *src);
        param->setDefault(gps::elem::Buffer::fromPod(0.5f));
        return true;
    }

    bool onSinkCaps(gps::elem::SinkPort& port, const gps::elem::Caps& caps) override {
        if (port.name() == "sink_video") {
            auto info = gps::elem::VideoInfo::fromCaps(caps);
            if (!info.has_value()) {
                return false;
            }
            {
                std::lock_guard<std::mutex> lock(m_infoMutex);
                m_videoInfo = *info;
            }
            if (auto* src = srcPort("src")) {
                src->setCaps(caps);
            }
        }
        return true;
    }

    bool start() override {
        auto* vid = sinkPort("sink_video");
        auto* param = sinkPort("sink_param");
        auto* src = srcPort("src");
        if (!vid || !param || !src) {
            return false;
        }
        m_driver.setUnblockCallback([vid]() { vid->unblock(); });
        return m_driver.start(this, [this, vid, param, src]() {
            auto bufOpt = vid->pop(GST_CLOCK_TIME_NONE);
            if (!bufOpt.has_value()) {
                if (vid->isEos()) {
                    src->pushEos();
                }
                m_driver.pause();
                return;
            }
            std::optional<gps::elem::VideoInfo> vinfo;
            {
                std::lock_guard<std::mutex> lock(m_infoMutex);
                vinfo = m_videoInfo;
            }
            if (!vinfo.has_value()) {
                return;
            }
            float alpha = 0.5f;
            if (auto pBuf = param->latest()) {
                if (auto val = pBuf->asPod<float>()) {
                    alpha = std::clamp(*val, 0.0f, 1.0f);
                }
            }
            gps::elem::Buffer outBuf = std::move(*bufOpt);
            if (auto frame = gps::elem::VideoFrame::mapWrite(*vinfo, outBuf)) {
                const int cx = frame->width() / 2;
                const int cy = frame->height() / 2;
                const uint8_t mark = static_cast<uint8_t>(255.0f * alpha);
                if (uint8_t* row = frame->row(0, cy)) {
                    const int ps = frame->pixelStride(0);
                    row[cx * ps] = mark;
                }
                frame->unmap();
            }
            GstFlowReturn ret = src->push(std::move(outBuf));
            if (ret != GST_FLOW_OK) {
                m_driver.pause();
            }
        });
    }

    void unblock() override { m_driver.unblock(); }
    bool stop() override {
        m_driver.stop();
        return true;
    }

private:
    std::mutex m_infoMutex;
    std::optional<gps::elem::VideoInfo> m_videoInfo;
    gps::elem::TaskDriver m_driver;
};

}  // namespace

GPS_ELEM_DEFINE_PLUGIN(
    reticleoverlay,
    "Reticle overlay 2-to-1 element built on gps_element_sdk",
    "1.0.0",
    "LGPL",
    "gps_element_sdk",
    "https://github.com/GstPipeStudio",
    {
        return gps::elem::registerElement<ReticleOverlay>(plugin, "reticleoverlay", GST_RANK_NONE) ? TRUE : FALSE;
    })
