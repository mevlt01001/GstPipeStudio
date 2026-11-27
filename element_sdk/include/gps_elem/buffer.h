#pragma once

#include <gst/gst.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>

namespace gps::elem {

class Buffer {
public:
    class ReadMap {
    public:
        ReadMap() = default;
        ~ReadMap();
        ReadMap(const ReadMap&) = delete;
        ReadMap& operator=(const ReadMap&) = delete;
        ReadMap(ReadMap&& other) noexcept;
        ReadMap& operator=(ReadMap&& other) noexcept;

        bool valid() const noexcept { return m_mapped; }
        const uint8_t* data() const noexcept { return m_mapped ? m_info.data : nullptr; }
        size_t size() const noexcept { return m_mapped ? m_info.size : 0; }
        void unmap() noexcept;

    private:
        friend class Buffer;
        explicit ReadMap(GstBuffer* buf) noexcept;

        GstBuffer* m_buf = nullptr;
        GstMapInfo m_info{};
        bool m_mapped = false;
    };

    class WriteMap {
    public:
        WriteMap() = default;
        ~WriteMap();
        WriteMap(const WriteMap&) = delete;
        WriteMap& operator=(const WriteMap&) = delete;
        WriteMap(WriteMap&& other) noexcept;
        WriteMap& operator=(WriteMap&& other) noexcept;

        bool valid() const noexcept { return m_mapped; }
        uint8_t* data() noexcept { return m_mapped ? m_info.data : nullptr; }
        const uint8_t* data() const noexcept { return m_mapped ? m_info.data : nullptr; }
        size_t size() const noexcept { return m_mapped ? m_info.size : 0; }
        void unmap() noexcept;

    private:
        friend class Buffer;
        explicit WriteMap(GstBuffer* buf) noexcept;

        GstBuffer* m_buf = nullptr;
        GstMapInfo m_info{};
        bool m_mapped = false;
    };

    Buffer() = default;
    ~Buffer();

    Buffer(const Buffer& other);
    Buffer& operator=(const Buffer& other);
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    static Buffer adopt(GstBuffer* buf) noexcept { return Buffer(buf); }
    static Buffer borrow(GstBuffer* buf) noexcept {
        return Buffer(buf ? gst_buffer_ref(buf) : nullptr);
    }

    static Buffer allocate(size_t size);

    template <typename T>
    static Buffer fromPod(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "Buffer::fromPod requires trivially copyable type");
        Buffer buf = allocate(sizeof(T));
        if (buf.isValid()) {
            auto map = buf.write();
            if (map.valid() && map.size() >= sizeof(T)) {
                std::memcpy(map.data(), &value, sizeof(T));
            }
        }
        return buf;
    }

    template <typename T>
    std::optional<T> asPod() const {
        static_assert(std::is_trivially_copyable_v<T>, "Buffer::asPod requires trivially copyable type");
        auto map = read();
        if (!map.valid() || map.size() < sizeof(T)) {
            return std::nullopt;
        }
        T out{};
        std::memcpy(&out, map.data(), sizeof(T));
        return out;
    }

    bool isValid() const noexcept { return m_buf != nullptr; }
    bool isWritable() const noexcept;
    void ensureWritable() noexcept;

    ReadMap read() const noexcept;
    WriteMap write() noexcept;

    size_t size() const noexcept;

    GstClockTime pts() const noexcept;
    void setPts(GstClockTime pts) noexcept;

    GstClockTime dts() const noexcept;
    void setDts(GstClockTime dts) noexcept;

    GstClockTime duration() const noexcept;
    void setDuration(GstClockTime duration) noexcept;

    GstBufferFlags flags() const noexcept;
    void setFlags(GstBufferFlags flags) noexcept;
    void unsetFlags(GstBufferFlags flags) noexcept;

    bool isDeltaUnit() const noexcept;
    void setDeltaUnit(bool on = true) noexcept;
    bool isDiscont() const noexcept;
    void setDiscont(bool on = true) noexcept;
    bool isHeader() const noexcept;
    void setHeader(bool on = true) noexcept;
    bool isGapFlags() const noexcept;
    void setGapFlags(bool on = true) noexcept;

    void copyMetadataFrom(
        const Buffer& src,
        GstBufferCopyFlags flags = static_cast<GstBufferCopyFlags>(
            GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS | GST_BUFFER_COPY_META)) noexcept;

    GstBuffer* get() const noexcept { return m_buf; }
    GstBuffer* release() noexcept;

private:
    explicit Buffer(GstBuffer* buf) noexcept : m_buf(buf) {}
    GstBuffer* m_buf = nullptr;
};

class BufferList {
public:
    BufferList() = default;
    ~BufferList();

    BufferList(const BufferList& other);
    BufferList& operator=(const BufferList& other);
    BufferList(BufferList&& other) noexcept;
    BufferList& operator=(BufferList&& other) noexcept;

    static BufferList adopt(GstBufferList* list) noexcept { return BufferList(list); }
    static BufferList borrow(GstBufferList* list) noexcept {
        return BufferList(list ? gst_buffer_list_ref(list) : nullptr);
    }
    static BufferList create();
    static BufferList createSized(size_t reserveSize);

    bool isValid() const noexcept { return m_list != nullptr; }
    bool isWritable() const noexcept;
    void ensureWritable() noexcept;

    size_t size() const noexcept;
    Buffer get(size_t index) const noexcept;
    void add(Buffer buf) noexcept;

    GstBufferList* raw() const noexcept { return m_list; }
    GstBufferList* release() noexcept;

private:
    explicit BufferList(GstBufferList* list) noexcept : m_list(list) {}
    GstBufferList* m_list = nullptr;
};

}  // namespace gps::elem
