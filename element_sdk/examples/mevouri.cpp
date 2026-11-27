#include "gps_elem/archetypes.h"
#include "gps_elem/register.h"

#include <cstring>
#include <mutex>
#include <string>

namespace {

class MevoUriSrc : public gps::elem::SourceElement {
public:
    explicit MevoUriSrc(GstElement* raw) : SourceElement(raw) {}

    static void describe(gps::elem::ElementClassBuilder& b) {
        b.setMetadata(
            "Mevo URI Source",
            "Source/Network",
            "URI handler source for mevo:// streams built on gps_element_sdk",
            "GstPipeStudio");
        addDefaultPorts(b, gps::elem::Caps::any());
        b.enableUriHandler(GST_URI_SRC, {"mevo"});
    }

    std::string currentUri() const override {
        std::lock_guard<std::mutex> lock(m_uriMutex);
        return m_uri;
    }

    bool setUri(const std::string& uri, std::string& errorOut) override {
        constexpr const char* kPrefix = "mevo://";
        if (uri.rfind(kPrefix, 0) != 0 || uri.size() <= std::strlen(kPrefix)) {
            errorOut = "Invalid mevo URI (expected mevo://<target>)";
            return false;
        }
        std::lock_guard<std::mutex> lock(m_uriMutex);
        m_uri = uri;
        m_index = 0;
        return true;
    }

    bool start() override {
        m_index = 0;
        if (auto* src = srcPort("src")) {
            src->setCaps(gps::elem::Caps::custom("application/x-mevo"));
        }
        return SourceElement::start();
    }

    GstFlowReturn produce(gps::elem::SrcPort& src) override {
        if (m_index >= 3) {
            return GST_FLOW_EOS;
        }
        std::string payload;
        {
            std::lock_guard<std::mutex> lock(m_uriMutex);
            payload = m_uri.empty() ? "mevo://default" : m_uri;
        }
        gps::elem::Buffer buf = gps::elem::Buffer::allocate(payload.size());
        {
            auto map = buf.write();
            if (map.valid()) {
                std::memcpy(map.data(), payload.data(), payload.size());
            }
        }
        buf.setPts(m_index * 33 * GST_MSECOND);
        buf.setDuration(33 * GST_MSECOND);
        ++m_index;
        return src.push(std::move(buf));
    }

private:
    mutable std::mutex m_uriMutex;
    std::string m_uri;
    uint64_t m_index = 0;
};

}  // namespace

GPS_ELEM_DEFINE_PLUGIN(
    mevouri,
    "Mevo URI handler source plugin built on gps_element_sdk",
    "1.0.0",
    "LGPL",
    "gps_element_sdk",
    "https://github.com/GstPipeStudio",
    {
        return gps::elem::registerElement<MevoUriSrc>(plugin, "mevourisrc", GST_RANK_PRIMARY) ? TRUE : FALSE;
    })
