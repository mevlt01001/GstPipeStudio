#pragma once

#include "gps_elem/buffer.h"
#include "gps_elem/caps.h"

#include <gst/audio/audio.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace gps::elem {

class AudioInfo {
public:
    AudioInfo();

    static std::optional<AudioInfo> fromCaps(const Caps& caps);
    static std::optional<AudioInfo> fromFormat(
        GstAudioFormat format,
        int rate,
        int channels,
        const GstAudioChannelPosition* positions = nullptr);

    Caps toCaps() const;

    GstAudioFormat format() const noexcept { return GST_AUDIO_INFO_FORMAT(&m_info); }
    std::string formatName() const;
    int rate() const noexcept { return GST_AUDIO_INFO_RATE(&m_info); }
    int channels() const noexcept { return GST_AUDIO_INFO_CHANNELS(&m_info); }
    int bpf() const noexcept { return GST_AUDIO_INFO_BPF(&m_info); }
    int bps() const noexcept { return GST_AUDIO_INFO_BPS(&m_info); }
    GstAudioLayout layout() const noexcept { return GST_AUDIO_INFO_LAYOUT(&m_info); }
    bool isInterleaved() const noexcept { return layout() == GST_AUDIO_LAYOUT_INTERLEAVED; }
    bool isPlanar() const noexcept { return layout() == GST_AUDIO_LAYOUT_NON_INTERLEAVED; }

    const GstAudioInfo* raw() const noexcept { return &m_info; }
    GstAudioInfo* raw() noexcept { return &m_info; }

private:
    GstAudioInfo m_info{};
};

class AudioFrame {
public:
    AudioFrame() = default;
    ~AudioFrame();

    AudioFrame(const AudioFrame&) = delete;
    AudioFrame& operator=(const AudioFrame&) = delete;
    AudioFrame(AudioFrame&& other) noexcept;
    AudioFrame& operator=(AudioFrame&& other) noexcept;

    static std::optional<AudioFrame> mapRead(const AudioInfo& info, const Buffer& buf);
    static std::optional<AudioFrame> mapWrite(const AudioInfo& info, Buffer& buf);

    void unmap() noexcept;
    bool valid() const noexcept { return m_mapped; }

    int samples() const noexcept { return m_mapped ? static_cast<int>(m_audioBuf.n_samples) : 0; }
    int planes() const noexcept { return m_mapped ? static_cast<int>(m_audioBuf.n_planes) : 0; }
    int channels() const noexcept { return m_mapped ? GST_AUDIO_INFO_CHANNELS(&m_audioBuf.info) : 0; }
    int rate() const noexcept { return m_mapped ? GST_AUDIO_INFO_RATE(&m_audioBuf.info) : 0; }
    int bpf() const noexcept { return m_mapped ? GST_AUDIO_INFO_BPF(&m_audioBuf.info) : 0; }
    bool isInterleaved() const noexcept {
        return m_mapped && GST_AUDIO_INFO_LAYOUT(&m_audioBuf.info) == GST_AUDIO_LAYOUT_INTERLEAVED;
    }
    bool isPlanar() const noexcept {
        return m_mapped && GST_AUDIO_INFO_LAYOUT(&m_audioBuf.info) == GST_AUDIO_LAYOUT_NON_INTERLEAVED;
    }

    size_t planeSizeBytes(int plane = 0) const noexcept;
    const uint8_t* planeData(int plane = 0) const noexcept;
    uint8_t* planeData(int plane = 0) noexcept;

    template <typename T>
    const T* samplesAs(int plane = 0) const noexcept {
        return reinterpret_cast<const T*>(planeData(plane));
    }

    template <typename T>
    T* samplesAs(int plane = 0) noexcept {
        return reinterpret_cast<T*>(planeData(plane));
    }

private:
    GstAudioBuffer m_audioBuf{};
    bool m_mapped = false;
    bool m_writable = false;
};

}  // namespace gps::elem
