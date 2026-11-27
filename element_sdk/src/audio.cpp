#include "gps_elem/audio.h"

#include <utility>

namespace gps::elem {

AudioInfo::AudioInfo() {
    gst_audio_info_init(&m_info);
}

std::optional<AudioInfo> AudioInfo::fromCaps(const Caps& caps) {
    if (!caps.isValid()) {
        return std::nullopt;
    }
    AudioInfo info;
    if (!gst_audio_info_from_caps(&info.m_info, caps.get())) {
        return std::nullopt;
    }
    return info;
}

std::optional<AudioInfo> AudioInfo::fromFormat(
    GstAudioFormat format,
    int rate,
    int channels,
    const GstAudioChannelPosition* positions) {
    AudioInfo info;
    gst_audio_info_set_format(&info.m_info, format, rate, channels, positions);
    if (GST_AUDIO_INFO_FORMAT(&info.m_info) == GST_AUDIO_FORMAT_UNKNOWN) {
        return std::nullopt;
    }
    return info;
}

Caps AudioInfo::toCaps() const {
    return Caps::adopt(gst_audio_info_to_caps(&m_info));
}

std::string AudioInfo::formatName() const {
    const gchar* name = gst_audio_format_to_string(format());
    return name ? std::string(name) : std::string("UNKNOWN");
}

// --- AudioFrame ---

AudioFrame::~AudioFrame() {
    unmap();
}

AudioFrame::AudioFrame(AudioFrame&& other) noexcept
    : m_audioBuf(other.m_audioBuf),
      m_mapped(std::exchange(other.m_mapped, false)),
      m_writable(std::exchange(other.m_writable, false)) {
    if (m_mapped) {
        if (other.m_audioBuf.planes == other.m_audioBuf.priv_planes_arr) {
            m_audioBuf.planes = m_audioBuf.priv_planes_arr;
        }
        if (other.m_audioBuf.map_infos == other.m_audioBuf.priv_map_infos_arr) {
            m_audioBuf.map_infos = m_audioBuf.priv_map_infos_arr;
        }
    }
    other.m_audioBuf = GstAudioBuffer{};
}

AudioFrame& AudioFrame::operator=(AudioFrame&& other) noexcept {
    if (this != &other) {
        unmap();
        m_audioBuf = other.m_audioBuf;
        m_mapped = std::exchange(other.m_mapped, false);
        m_writable = std::exchange(other.m_writable, false);
        if (m_mapped) {
            if (other.m_audioBuf.planes == other.m_audioBuf.priv_planes_arr) {
                m_audioBuf.planes = m_audioBuf.priv_planes_arr;
            }
            if (other.m_audioBuf.map_infos == other.m_audioBuf.priv_map_infos_arr) {
                m_audioBuf.map_infos = m_audioBuf.priv_map_infos_arr;
            }
        }
        other.m_audioBuf = GstAudioBuffer{};
    }
    return *this;
}

std::optional<AudioFrame> AudioFrame::mapRead(const AudioInfo& info, const Buffer& buf) {
    if (!buf.isValid()) {
        return std::nullopt;
    }
    std::optional<AudioFrame> out;
    out.emplace();
    if (!gst_audio_buffer_map(
            &out->m_audioBuf,
            const_cast<GstAudioInfo*>(info.raw()),
            buf.get(),
            GST_MAP_READ)) {
        return std::nullopt;
    }
    out->m_mapped = true;
    out->m_writable = false;
    return out;
}

std::optional<AudioFrame> AudioFrame::mapWrite(const AudioInfo& info, Buffer& buf) {
    if (!buf.isValid()) {
        return std::nullopt;
    }
    buf.ensureWritable();
    std::optional<AudioFrame> out;
    out.emplace();
    if (!gst_audio_buffer_map(
            &out->m_audioBuf,
            const_cast<GstAudioInfo*>(info.raw()),
            buf.get(),
            GST_MAP_READWRITE)) {
        return std::nullopt;
    }
    out->m_mapped = true;
    out->m_writable = true;
    return out;
}

void AudioFrame::unmap() noexcept {
    if (m_mapped) {
        gst_audio_buffer_unmap(&m_audioBuf);
        m_mapped = false;
        m_writable = false;
    }
}

size_t AudioFrame::planeSizeBytes(int plane) const noexcept {
    if (!m_mapped || plane < 0 || plane >= m_audioBuf.n_planes) {
        return 0;
    }
    if (isPlanar()) {
        return static_cast<size_t>(m_audioBuf.n_samples) * static_cast<size_t>(GST_AUDIO_INFO_BPS(&m_audioBuf.info));
    }
    return static_cast<size_t>(m_audioBuf.n_samples) * static_cast<size_t>(GST_AUDIO_INFO_BPF(&m_audioBuf.info));
}

const uint8_t* AudioFrame::planeData(int plane) const noexcept {
    if (!m_mapped || plane < 0 || plane >= m_audioBuf.n_planes || !m_audioBuf.planes) {
        return nullptr;
    }
    return static_cast<const uint8_t*>(m_audioBuf.planes[plane]);
}

uint8_t* AudioFrame::planeData(int plane) noexcept {
    if (!m_mapped || !m_writable || plane < 0 || plane >= m_audioBuf.n_planes || !m_audioBuf.planes) {
        return nullptr;
    }
    return static_cast<uint8_t*>(m_audioBuf.planes[plane]);
}

}  // namespace gps::elem
