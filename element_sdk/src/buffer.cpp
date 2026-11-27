#include "gps_elem/buffer.h"

#include <utility>

namespace gps::elem {

// --- ReadMap ---

Buffer::ReadMap::ReadMap(GstBuffer* buf) noexcept : m_buf(buf) {
    if (m_buf) {
        m_mapped = gst_buffer_map(m_buf, &m_info, GST_MAP_READ) != FALSE;
    }
}

Buffer::ReadMap::~ReadMap() {
    unmap();
}

Buffer::ReadMap::ReadMap(ReadMap&& other) noexcept
    : m_buf(std::exchange(other.m_buf, nullptr)),
      m_info(other.m_info),
      m_mapped(std::exchange(other.m_mapped, false)) {}

Buffer::ReadMap& Buffer::ReadMap::operator=(ReadMap&& other) noexcept {
    if (this != &other) {
        unmap();
        m_buf = std::exchange(other.m_buf, nullptr);
        m_info = other.m_info;
        m_mapped = std::exchange(other.m_mapped, false);
    }
    return *this;
}

void Buffer::ReadMap::unmap() noexcept {
    if (m_mapped && m_buf) {
        gst_buffer_unmap(m_buf, &m_info);
    }
    m_mapped = false;
    m_buf = nullptr;
}

// --- WriteMap ---

Buffer::WriteMap::WriteMap(GstBuffer* buf) noexcept : m_buf(buf) {
    if (m_buf) {
        m_mapped = gst_buffer_map(m_buf, &m_info, GST_MAP_READWRITE) != FALSE;
    }
}

Buffer::WriteMap::~WriteMap() {
    unmap();
}

Buffer::WriteMap::WriteMap(WriteMap&& other) noexcept
    : m_buf(std::exchange(other.m_buf, nullptr)),
      m_info(other.m_info),
      m_mapped(std::exchange(other.m_mapped, false)) {}

Buffer::WriteMap& Buffer::WriteMap::operator=(WriteMap&& other) noexcept {
    if (this != &other) {
        unmap();
        m_buf = std::exchange(other.m_buf, nullptr);
        m_info = other.m_info;
        m_mapped = std::exchange(other.m_mapped, false);
    }
    return *this;
}

void Buffer::WriteMap::unmap() noexcept {
    if (m_mapped && m_buf) {
        gst_buffer_unmap(m_buf, &m_info);
    }
    m_mapped = false;
    m_buf = nullptr;
}

// --- Buffer ---

Buffer::~Buffer() {
    if (m_buf) {
        gst_buffer_unref(m_buf);
    }
}

Buffer::Buffer(const Buffer& other)
    : m_buf(other.m_buf ? gst_buffer_ref(other.m_buf) : nullptr) {}

Buffer& Buffer::operator=(const Buffer& other) {
    if (this != &other) {
        if (m_buf) {
            gst_buffer_unref(m_buf);
        }
        m_buf = other.m_buf ? gst_buffer_ref(other.m_buf) : nullptr;
    }
    return *this;
}

Buffer::Buffer(Buffer&& other) noexcept : m_buf(std::exchange(other.m_buf, nullptr)) {}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        if (m_buf) {
            gst_buffer_unref(m_buf);
        }
        m_buf = std::exchange(other.m_buf, nullptr);
    }
    return *this;
}

Buffer Buffer::allocate(size_t size) {
    return Buffer(gst_buffer_new_allocate(nullptr, size, nullptr));
}

bool Buffer::isWritable() const noexcept {
    return m_buf && gst_buffer_is_writable(m_buf);
}

void Buffer::ensureWritable() noexcept {
    if (m_buf) {
        m_buf = gst_buffer_make_writable(m_buf);
    }
}

Buffer::ReadMap Buffer::read() const noexcept {
    return ReadMap(m_buf);
}

Buffer::WriteMap Buffer::write() noexcept {
    ensureWritable();
    return WriteMap(m_buf);
}

size_t Buffer::size() const noexcept {
    return m_buf ? gst_buffer_get_size(m_buf) : 0;
}

GstClockTime Buffer::pts() const noexcept {
    return m_buf ? GST_BUFFER_PTS(m_buf) : GST_CLOCK_TIME_NONE;
}

void Buffer::setPts(GstClockTime pts) noexcept {
    if (m_buf) {
        ensureWritable();
        GST_BUFFER_PTS(m_buf) = pts;
    }
}

GstClockTime Buffer::dts() const noexcept {
    return m_buf ? GST_BUFFER_DTS(m_buf) : GST_CLOCK_TIME_NONE;
}

void Buffer::setDts(GstClockTime dts) noexcept {
    if (m_buf) {
        ensureWritable();
        GST_BUFFER_DTS(m_buf) = dts;
    }
}

GstClockTime Buffer::duration() const noexcept {
    return m_buf ? GST_BUFFER_DURATION(m_buf) : GST_CLOCK_TIME_NONE;
}

void Buffer::setDuration(GstClockTime duration) noexcept {
    if (m_buf) {
        ensureWritable();
        GST_BUFFER_DURATION(m_buf) = duration;
    }
}

GstBufferFlags Buffer::flags() const noexcept {
    return m_buf ? static_cast<GstBufferFlags>(GST_BUFFER_FLAGS(m_buf)) : static_cast<GstBufferFlags>(0);
}

void Buffer::setFlags(GstBufferFlags flags) noexcept {
    if (m_buf) {
        ensureWritable();
        GST_BUFFER_FLAG_SET(m_buf, flags);
    }
}

void Buffer::unsetFlags(GstBufferFlags flags) noexcept {
    if (m_buf) {
        ensureWritable();
        GST_BUFFER_FLAG_UNSET(m_buf, flags);
    }
}

bool Buffer::isDeltaUnit() const noexcept {
    return m_buf && GST_BUFFER_FLAG_IS_SET(m_buf, GST_BUFFER_FLAG_DELTA_UNIT);
}

void Buffer::setDeltaUnit(bool on) noexcept {
    if (on) setFlags(GST_BUFFER_FLAG_DELTA_UNIT);
    else unsetFlags(GST_BUFFER_FLAG_DELTA_UNIT);
}

bool Buffer::isDiscont() const noexcept {
    return m_buf && GST_BUFFER_FLAG_IS_SET(m_buf, GST_BUFFER_FLAG_DISCONT);
}

void Buffer::setDiscont(bool on) noexcept {
    if (on) setFlags(GST_BUFFER_FLAG_DISCONT);
    else unsetFlags(GST_BUFFER_FLAG_DISCONT);
}

bool Buffer::isHeader() const noexcept {
    return m_buf && GST_BUFFER_FLAG_IS_SET(m_buf, GST_BUFFER_FLAG_HEADER);
}

void Buffer::setHeader(bool on) noexcept {
    if (on) setFlags(GST_BUFFER_FLAG_HEADER);
    else unsetFlags(GST_BUFFER_FLAG_HEADER);
}

bool Buffer::isGapFlags() const noexcept {
    return m_buf && GST_BUFFER_FLAG_IS_SET(m_buf, GST_BUFFER_FLAG_GAP);
}

void Buffer::setGapFlags(bool on) noexcept {
    if (on) setFlags(GST_BUFFER_FLAG_GAP);
    else unsetFlags(GST_BUFFER_FLAG_GAP);
}

void Buffer::copyMetadataFrom(const Buffer& src, GstBufferCopyFlags flags) noexcept {
    if (m_buf && src.m_buf) {
        ensureWritable();
        gst_buffer_copy_into(m_buf, src.m_buf, flags, 0, static_cast<gsize>(-1));
    }
}

GstBuffer* Buffer::release() noexcept {
    return std::exchange(m_buf, nullptr);
}

// --- BufferList ---

BufferList::~BufferList() {
    if (m_list) {
        gst_buffer_list_unref(m_list);
    }
}

BufferList::BufferList(const BufferList& other)
    : m_list(other.m_list ? gst_buffer_list_ref(other.m_list) : nullptr) {}

BufferList& BufferList::operator=(const BufferList& other) {
    if (this != &other) {
        if (m_list) {
            gst_buffer_list_unref(m_list);
        }
        m_list = other.m_list ? gst_buffer_list_ref(other.m_list) : nullptr;
    }
    return *this;
}

BufferList::BufferList(BufferList&& other) noexcept
    : m_list(std::exchange(other.m_list, nullptr)) {}

BufferList& BufferList::operator=(BufferList&& other) noexcept {
    if (this != &other) {
        if (m_list) {
            gst_buffer_list_unref(m_list);
        }
        m_list = std::exchange(other.m_list, nullptr);
    }
    return *this;
}

BufferList BufferList::create() {
    return BufferList(gst_buffer_list_new());
}

BufferList BufferList::createSized(size_t reserveSize) {
    return BufferList(gst_buffer_list_new_sized(static_cast<guint>(reserveSize)));
}

bool BufferList::isWritable() const noexcept {
    return m_list && gst_buffer_list_is_writable(m_list);
}

void BufferList::ensureWritable() noexcept {
    if (m_list) {
        m_list = gst_buffer_list_make_writable(m_list);
    }
}

size_t BufferList::size() const noexcept {
    return m_list ? gst_buffer_list_length(m_list) : 0;
}

Buffer BufferList::get(size_t index) const noexcept {
    if (!m_list || index >= size()) {
        return Buffer();
    }
    return Buffer::borrow(gst_buffer_list_get(m_list, static_cast<guint>(index)));
}

void BufferList::add(Buffer buf) noexcept {
    if (!m_list || !buf.isValid()) {
        return;
    }
    ensureWritable();
    gst_buffer_list_add(m_list, buf.release());
}

GstBufferList* BufferList::release() noexcept {
    return std::exchange(m_list, nullptr);
}

}  // namespace gps::elem
