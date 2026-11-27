#include "gps_elem/video.h"

#include <utility>

namespace gps::elem {

VideoInfo::VideoInfo() {
    gst_video_info_init(&m_info);
}

std::optional<VideoInfo> VideoInfo::fromCaps(const Caps& caps) {
    if (!caps.isValid()) {
        return std::nullopt;
    }
    VideoInfo info;
    if (!gst_video_info_from_caps(&info.m_info, caps.get())) {
        return std::nullopt;
    }
    return info;
}

std::optional<VideoInfo> VideoInfo::fromFormat(GstVideoFormat format, int width, int height, int fpsNum, int fpsDen) {
    VideoInfo info;
    if (!gst_video_info_set_format(&info.m_info, format, static_cast<guint>(width), static_cast<guint>(height))) {
        return std::nullopt;
    }
    info.m_info.fps_n = fpsNum;
    info.m_info.fps_d = fpsDen > 0 ? fpsDen : 1;
    return info;
}

Caps VideoInfo::toCaps() const {
    return Caps::adopt(gst_video_info_to_caps(&m_info));
}

std::string VideoInfo::formatName() const {
    const char* name = gst_video_format_to_string(format());
    return name ? std::string(name) : std::string("UNKNOWN");
}

// --- VideoFrame ---

VideoFrame::~VideoFrame() {
    unmap();
}

VideoFrame::VideoFrame(VideoFrame&& other) noexcept
    : m_frame(other.m_frame),
      m_mapped(std::exchange(other.m_mapped, false)),
      m_writable(std::exchange(other.m_writable, false)) {}

VideoFrame& VideoFrame::operator=(VideoFrame&& other) noexcept {
    if (this != &other) {
        unmap();
        m_frame = other.m_frame;
        m_mapped = std::exchange(other.m_mapped, false);
        m_writable = std::exchange(other.m_writable, false);
    }
    return *this;
}

std::optional<VideoFrame> VideoFrame::mapRead(const VideoInfo& info, const Buffer& buf) {
    if (!buf.isValid()) {
        return std::nullopt;
    }
    VideoFrame vf;
    if (!gst_video_frame_map(&vf.m_frame, const_cast<GstVideoInfo*>(info.raw()), buf.get(), GST_MAP_READ)) {
        return std::nullopt;
    }
    vf.m_mapped = true;
    vf.m_writable = false;
    return vf;
}

std::optional<VideoFrame> VideoFrame::mapWrite(const VideoInfo& info, Buffer& buf) {
    if (!buf.isValid()) {
        return std::nullopt;
    }
    buf.ensureWritable();
    VideoFrame vf;
    if (!gst_video_frame_map(&vf.m_frame, const_cast<GstVideoInfo*>(info.raw()), buf.get(), GST_MAP_READWRITE)) {
        return std::nullopt;
    }
    vf.m_mapped = true;
    vf.m_writable = true;
    return vf;
}

void VideoFrame::unmap() noexcept {
    if (m_mapped) {
        gst_video_frame_unmap(&m_frame);
        m_mapped = false;
        m_writable = false;
    }
}

GstVideoFormat VideoFrame::format() const noexcept {
    return m_mapped ? GST_VIDEO_FRAME_FORMAT(&m_frame) : GST_VIDEO_FORMAT_UNKNOWN;
}

int VideoFrame::width() const noexcept {
    return m_mapped ? GST_VIDEO_FRAME_WIDTH(&m_frame) : 0;
}

int VideoFrame::height() const noexcept {
    return m_mapped ? GST_VIDEO_FRAME_HEIGHT(&m_frame) : 0;
}

int VideoFrame::planeCount() const noexcept {
    return m_mapped ? static_cast<int>(GST_VIDEO_FRAME_N_PLANES(&m_frame)) : 0;
}

const uint8_t* VideoFrame::planeData(int plane) const noexcept {
    if (!m_mapped || plane < 0 || plane >= planeCount()) {
        return nullptr;
    }
    return static_cast<const uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&m_frame, static_cast<guint>(plane)));
}

uint8_t* VideoFrame::planeData(int plane) noexcept {
    if (!m_mapped || !m_writable || plane < 0 || plane >= planeCount()) {
        return nullptr;
    }
    return static_cast<uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(&m_frame, static_cast<guint>(plane)));
}

int VideoFrame::planeStride(int plane) const noexcept {
    if (!m_mapped || plane < 0 || plane >= planeCount()) {
        return 0;
    }
    return GST_VIDEO_FRAME_PLANE_STRIDE(&m_frame, static_cast<guint>(plane));
}

int VideoFrame::pixelStride(int plane) const noexcept {
    if (!m_mapped || plane < 0 || plane >= planeCount()) {
        return 0;
    }
    return GST_VIDEO_FRAME_COMP_PSTRIDE(&m_frame, static_cast<guint>(plane));
}

const uint8_t* VideoFrame::row(int plane, int y) const noexcept {
    const uint8_t* base = planeData(plane);
    if (!base || y < 0 || y >= height()) {
        return nullptr;
    }
    return base + static_cast<ptrdiff_t>(y) * planeStride(plane);
}

uint8_t* VideoFrame::row(int plane, int y) noexcept {
    uint8_t* base = planeData(plane);
    if (!base || y < 0 || y >= height()) {
        return nullptr;
    }
    return base + static_cast<ptrdiff_t>(y) * planeStride(plane);
}

}  // namespace gps::elem
