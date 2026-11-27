#include "gps_elem/caps.h"
#include "gps_elem/buffer.h"

#include <utility>

namespace gps::elem {

namespace {

void applyFeatures(GstCaps* caps, const std::vector<std::string>& features) {
    if (!caps || features.empty() || gst_caps_get_size(caps) == 0) {
        return;
    }
    GstCapsFeatures* feats = gst_caps_features_new_empty();
    for (const auto& f : features) {
        gst_caps_features_add(feats, f.c_str());
    }
    gst_caps_set_features(caps, 0, feats);
}

void setStringListOrSingle(GstStructure* s, const char* field, const std::vector<std::string>& values) {
    if (values.size() == 1) {
        gst_structure_set(s, field, G_TYPE_STRING, values[0].c_str(), nullptr);
    } else if (values.size() > 1) {
        GValue list = G_VALUE_INIT;
        g_value_init(&list, GST_TYPE_LIST);
        for (const auto& val : values) {
            GValue item = G_VALUE_INIT;
            g_value_init(&item, G_TYPE_STRING);
            g_value_set_string(&item, val.c_str());
            gst_value_list_append_value(&list, &item);
            g_value_unset(&item);
        }
        gst_structure_take_value(s, field, &list);
    }
}

}  // namespace

Caps::~Caps() {
    if (m_caps) {
        gst_caps_unref(m_caps);
    }
}

Caps::Caps(const Caps& other)
    : m_caps(other.m_caps ? gst_caps_ref(other.m_caps) : nullptr) {}

Caps& Caps::operator=(const Caps& other) {
    if (this != &other) {
        if (m_caps) {
            gst_caps_unref(m_caps);
        }
        m_caps = other.m_caps ? gst_caps_ref(other.m_caps) : nullptr;
    }
    return *this;
}

Caps::Caps(Caps&& other) noexcept : m_caps(std::exchange(other.m_caps, nullptr)) {}

Caps& Caps::operator=(Caps&& other) noexcept {
    if (this != &other) {
        if (m_caps) {
            gst_caps_unref(m_caps);
        }
        m_caps = std::exchange(other.m_caps, nullptr);
    }
    return *this;
}

Caps Caps::adopt(GstCaps* caps) {
    return Caps(caps);
}

Caps Caps::borrow(GstCaps* caps) {
    return Caps(caps ? gst_caps_ref(caps) : nullptr);
}

Caps Caps::any() {
    return Caps(gst_caps_new_any());
}

Caps Caps::empty() {
    return Caps(gst_caps_new_empty());
}

Caps Caps::fromString(const std::string& str) {
    return Caps(gst_caps_from_string(str.c_str()));
}

Caps Caps::videoRaw(const VideoRawSpec& spec) {
    GstStructure* s = gst_structure_new_empty("video/x-raw");
    setStringListOrSingle(s, "format", spec.formats);

    if (spec.width > 0) {
        gst_structure_set(s, "width", G_TYPE_INT, spec.width, nullptr);
    } else {
        gst_structure_set(s, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT, nullptr);
    }

    if (spec.height > 0) {
        gst_structure_set(s, "height", G_TYPE_INT, spec.height, nullptr);
    } else {
        gst_structure_set(s, "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, nullptr);
    }

    if (spec.fpsNum > 0) {
        gst_structure_set(s, "framerate", GST_TYPE_FRACTION, spec.fpsNum, spec.fpsDen > 0 ? spec.fpsDen : 1, nullptr);
    } else {
        gst_structure_set(s, "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, nullptr);
    }

    GstCaps* caps = gst_caps_new_full(s, nullptr);
    applyFeatures(caps, spec.features);
    return Caps(caps);
}

Caps Caps::audioRaw(const AudioRawSpec& spec) {
    GstStructure* s = gst_structure_new_empty("audio/x-raw");
    setStringListOrSingle(s, "format", spec.formats);

    if (spec.rate > 0) {
        gst_structure_set(s, "rate", G_TYPE_INT, spec.rate, nullptr);
    } else {
        gst_structure_set(s, "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT, nullptr);
    }

    if (spec.channels > 0) {
        gst_structure_set(s, "channels", G_TYPE_INT, spec.channels, nullptr);
    } else {
        gst_structure_set(s, "channels", GST_TYPE_INT_RANGE, 1, G_MAXINT, nullptr);
    }

    if (!spec.layout.empty()) {
        gst_structure_set(s, "layout", G_TYPE_STRING, spec.layout.c_str(), nullptr);
    }

    GstCaps* caps = gst_caps_new_full(s, nullptr);
    applyFeatures(caps, spec.features);
    return Caps(caps);
}

Caps Caps::custom(const std::string& mediaType) {
    return Caps(gst_caps_new_empty_simple(mediaType.c_str()));
}

Caps Caps::withFeatures(const std::string& mediaType, const std::vector<std::string>& features) {
    GstCaps* caps = gst_caps_new_empty_simple(mediaType.c_str());
    applyFeatures(caps, features);
    return Caps(caps);
}

void Caps::ensureWritable() noexcept {
    if (m_caps) {
        m_caps = gst_caps_make_writable(m_caps);
    }
}

bool Caps::isEmpty() const noexcept {
    return !m_caps || gst_caps_is_empty(m_caps);
}

bool Caps::isAny() const noexcept {
    return m_caps && gst_caps_is_any(m_caps);
}

bool Caps::isFixed() const noexcept {
    return m_caps && gst_caps_is_fixed(m_caps);
}

bool Caps::hasFeature(const std::string& feature) const noexcept {
    if (!m_caps || gst_caps_get_size(m_caps) == 0) {
        return false;
    }
    GstCapsFeatures* feats = gst_caps_get_features(m_caps, 0);
    return feats && gst_caps_features_contains(feats, feature.c_str()) != FALSE;
}

Caps& Caps::setInt(const std::string& field, int value) {
    if (m_caps && gst_caps_get_size(m_caps) > 0) {
        ensureWritable();
        GstStructure* s = gst_caps_get_structure(m_caps, 0);
        if (s) {
            gst_structure_set(s, field.c_str(), G_TYPE_INT, value, nullptr);
        }
    }
    return *this;
}

Caps& Caps::setString(const std::string& field, const std::string& value) {
    if (m_caps && gst_caps_get_size(m_caps) > 0) {
        ensureWritable();
        GstStructure* s = gst_caps_get_structure(m_caps, 0);
        if (s) {
            gst_structure_set(s, field.c_str(), G_TYPE_STRING, value.c_str(), nullptr);
        }
    }
    return *this;
}

Caps& Caps::setCodecData(const Buffer& buf) {
    if (m_caps && gst_caps_get_size(m_caps) > 0 && buf.isValid()) {
        ensureWritable();
        GstStructure* s = gst_caps_get_structure(m_caps, 0);
        if (s) {
            gst_structure_set(s, "codec_data", GST_TYPE_BUFFER, buf.get(), nullptr);
        }
    }
    return *this;
}

std::optional<int> Caps::optInt(const std::string& field) const {
    if (!m_caps || gst_caps_get_size(m_caps) == 0) {
        return std::nullopt;
    }
    const GstStructure* s = gst_caps_get_structure(m_caps, 0);
    gint val = 0;
    if (s && gst_structure_get_int(s, field.c_str(), &val) != FALSE) {
        return static_cast<int>(val);
    }
    return std::nullopt;
}

std::optional<std::string> Caps::optString(const std::string& field) const {
    if (!m_caps || gst_caps_get_size(m_caps) == 0) {
        return std::nullopt;
    }
    const GstStructure* s = gst_caps_get_structure(m_caps, 0);
    if (!s) {
        return std::nullopt;
    }
    const gchar* val = gst_structure_get_string(s, field.c_str());
    if (val) {
        return std::string(val);
    }
    return std::nullopt;
}

std::optional<Buffer> Caps::codecData() const {
    if (!m_caps || gst_caps_get_size(m_caps) == 0) {
        return std::nullopt;
    }
    const GstStructure* s = gst_caps_get_structure(m_caps, 0);
    if (!s) {
        return std::nullopt;
    }
    const GValue* val = gst_structure_get_value(s, "codec_data");
    if (val && GST_VALUE_HOLDS_BUFFER(val)) {
        GstBuffer* buf = gst_value_get_buffer(val);
        if (buf) {
            return Buffer::borrow(buf);
        }
    }
    return std::nullopt;
}

bool Caps::canIntersect(const Caps& other) const noexcept {
    if (!m_caps || !other.m_caps) {
        return false;
    }
    return gst_caps_can_intersect(m_caps, other.m_caps);
}

Caps Caps::intersect(const Caps& other, GstCapsIntersectMode mode) const {
    if (!m_caps || !other.m_caps) {
        return empty();
    }
    return Caps(gst_caps_intersect_full(m_caps, other.m_caps, mode));
}

Caps Caps::fixate() const {
    if (!m_caps) {
        return Caps();
    }
    return Caps(gst_caps_fixate(gst_caps_copy(m_caps)));
}

std::string Caps::toString() const {
    if (!m_caps) {
        return "(null)";
    }
    gchar* str = gst_caps_to_string(m_caps);
    std::string out(str ? str : "");
    g_free(str);
    return out;
}

GstCaps* Caps::release() noexcept {
    return std::exchange(m_caps, nullptr);
}

}  // namespace gps::elem
