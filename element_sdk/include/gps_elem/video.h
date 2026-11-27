#pragma once

#include "gps_elem/buffer.h"
#include "gps_elem/caps.h"

#include <gst/video/video.h>

#include <cstdint>
#include <optional>
#include <string>

namespace gps::elem {

class VideoInfo {
public:
    VideoInfo();

    static std::optional<VideoInfo> fromCaps(const Caps& caps);
    static std::optional<VideoInfo> fromFormat(GstVideoFormat format, int width, int height, int fpsNum = 30, int fpsDen = 1);

    Caps toCaps() const;

    GstVideoFormat format() const noexcept { return GST_VIDEO_INFO_FORMAT(&m_info); }
    std::string formatName() const;
    int width() const noexcept { return GST_VIDEO_INFO_WIDTH(&m_info); }
    int height() const noexcept { return GST_VIDEO_INFO_HEIGHT(&m_info); }
    size_t size() const noexcept { return GST_VIDEO_INFO_SIZE(&m_info); }
    int fpsNum() const noexcept { return GST_VIDEO_INFO_FPS_N(&m_info); }
    int fpsDen() const noexcept { return GST_VIDEO_INFO_FPS_D(&m_info); }
    int planeCount() const noexcept { return GST_VIDEO_INFO_N_PLANES(&m_info); }

    const GstVideoInfo* raw() const noexcept { return &m_info; }
    GstVideoInfo* raw() noexcept { return &m_info; }

private:
    GstVideoInfo m_info{};
};

class VideoFrame {
public:
    VideoFrame() = default;
    ~VideoFrame();

    VideoFrame(const VideoFrame&) = delete;
    VideoFrame& operator=(const VideoFrame&) = delete;
    VideoFrame(VideoFrame&& other) noexcept;
    VideoFrame& operator=(VideoFrame&& other) noexcept;

    static std::optional<VideoFrame> mapRead(const VideoInfo& info, const Buffer& buf);
    static std::optional<VideoFrame> mapWrite(const VideoInfo& info, Buffer& buf);

    void unmap() noexcept;
    bool valid() const noexcept { return m_mapped; }

    GstVideoFormat format() const noexcept;
    int width() const noexcept;
    int height() const noexcept;
    int planeCount() const noexcept;

    const uint8_t* planeData(int plane) const noexcept;
    uint8_t* planeData(int plane) noexcept;

    int planeStride(int plane) const noexcept;
    int pixelStride(int plane) const noexcept;

    const uint8_t* row(int plane, int y) const noexcept;
    uint8_t* row(int plane, int y) noexcept;

private:
    GstVideoFrame m_frame{};
    bool m_mapped = false;
    bool m_writable = false;
};

}  // namespace gps::elem
