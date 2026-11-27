#pragma once

#include <gst/gst.h>

#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace gps::elem {

class Buffer;

struct VideoRawSpec {
    std::vector<std::string> formats;
    int width = 0;      // 0 means [1, MAX]
    int height = 0;     // 0 means [1, MAX]
    int fpsNum = 0;     // 0 means [0/1, MAX/1]
    int fpsDen = 1;
    std::vector<std::string> features;
};

struct AudioRawSpec {
    std::vector<std::string> formats;
    int rate = 0;       // 0 means [1, MAX]
    int channels = 0;   // 0 means [1, MAX]
    std::string layout; // "interleaved", "non-interleaved", or empty
    std::vector<std::string> features;
};

class Caps {
public:
    Caps() = default;
    ~Caps();

    Caps(const Caps& other);
    Caps& operator=(const Caps& other);
    Caps(Caps&& other) noexcept;
    Caps& operator=(Caps&& other) noexcept;

    static Caps adopt(GstCaps* caps);
    static Caps borrow(GstCaps* caps);

    static Caps any();
    static Caps empty();
    static Caps fromString(const std::string& str);
    static Caps videoRaw(const VideoRawSpec& spec = {});
    static Caps audioRaw(const AudioRawSpec& spec = {});
    static Caps custom(const std::string& mediaType);
    static Caps withFeatures(const std::string& mediaType, const std::vector<std::string>& features);

    bool isValid() const noexcept { return m_caps != nullptr; }
    bool isEmpty() const noexcept;
    bool isAny() const noexcept;
    bool isFixed() const noexcept;
    bool hasFeature(const std::string& feature) const noexcept;

    Caps& setInt(const std::string& field, int value);
    Caps& setString(const std::string& field, const std::string& value);
    Caps& setCodecData(const Buffer& buf);

    std::optional<int> optInt(const std::string& field) const;
    std::optional<std::string> optString(const std::string& field) const;
    std::optional<Buffer> codecData() const;

    bool canIntersect(const Caps& other) const noexcept;
    Caps intersect(const Caps& other, GstCapsIntersectMode mode = GST_CAPS_INTERSECT_FIRST) const;
    Caps fixate() const;
    std::string toString() const;

    GstCaps* get() const noexcept { return m_caps; }
    GstCaps* release() noexcept;

private:
    void ensureWritable() noexcept;

    explicit Caps(GstCaps* caps) noexcept : m_caps(caps) {}
    GstCaps* m_caps = nullptr;
};

}  // namespace gps::elem
