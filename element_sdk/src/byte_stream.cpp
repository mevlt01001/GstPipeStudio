#include "gps_elem/byte_stream.h"

#include <utility>

namespace gps::elem {

ByteStreamAdapter::ByteStreamAdapter()
    : m_adapter(gst_adapter_new()) {}

ByteStreamAdapter::~ByteStreamAdapter() {
    if (m_adapter) {
        g_object_unref(m_adapter);
        m_adapter = nullptr;
    }
}

ByteStreamAdapter::ByteStreamAdapter(ByteStreamAdapter&& other) noexcept
    : m_adapter(std::exchange(other.m_adapter, nullptr)) {}

ByteStreamAdapter& ByteStreamAdapter::operator=(ByteStreamAdapter&& other) noexcept {
    if (this != &other) {
        if (m_adapter) {
            g_object_unref(m_adapter);
        }
        m_adapter = std::exchange(other.m_adapter, nullptr);
    }
    return *this;
}

void ByteStreamAdapter::push(Buffer buf) {
    if (m_adapter && buf.isValid()) {
        gst_adapter_push(m_adapter, buf.release());
    }
}

size_t ByteStreamAdapter::available() const noexcept {
    return m_adapter ? gst_adapter_available(m_adapter) : 0;
}

std::vector<uint8_t> ByteStreamAdapter::copyBytes(size_t offset, size_t size) const {
    std::vector<uint8_t> out;
    const size_t avail = available();
    if (!m_adapter || size == 0 || offset > avail || size > avail - offset) {
        return out;
    }
    out.resize(size);
    gst_adapter_copy(m_adapter, out.data(), offset, size);
    return out;
}

bool ByteStreamAdapter::copyBytesInto(void* dest, size_t offset, size_t size) const noexcept {
    const size_t avail = available();
    if (!m_adapter || !dest || offset > avail || size > avail - offset) {
        return false;
    }
    if (size > 0) {
        gst_adapter_copy(m_adapter, dest, offset, size);
    }
    return true;
}

gssize ByteStreamAdapter::maskedScanUint32(
    uint32_t mask,
    uint32_t pattern,
    size_t offset,
    size_t size) const noexcept {
    const size_t avail = available();
    if (!m_adapter || size == 0 || offset > avail || size > avail - offset) {
        return -1;
    }
    return gst_adapter_masked_scan_uint32(m_adapter, mask, pattern, offset, size);
}

void ByteStreamAdapter::flush(size_t nbytes) noexcept {
    if (m_adapter && nbytes > 0) {
        const size_t avail = available();
        gst_adapter_flush(m_adapter, nbytes > avail ? avail : nbytes);
    }
}

GstClockTime ByteStreamAdapter::prevPts(uint64_t* distance) const noexcept {
    if (!m_adapter) {
        if (distance) {
            *distance = 0;
        }
        return GST_CLOCK_TIME_NONE;
    }
    guint64 dist = 0;
    GstClockTime pts = gst_adapter_prev_pts(m_adapter, &dist);
    if (distance) {
        *distance = static_cast<uint64_t>(dist);
    }
    return pts;
}

GstClockTime ByteStreamAdapter::prevDts(uint64_t* distance) const noexcept {
    if (!m_adapter) {
        if (distance) {
            *distance = 0;
        }
        return GST_CLOCK_TIME_NONE;
    }
    guint64 dist = 0;
    GstClockTime dts = gst_adapter_prev_dts(m_adapter, &dist);
    if (distance) {
        *distance = static_cast<uint64_t>(dist);
    }
    return dts;
}

Buffer ByteStreamAdapter::takeBuffer(
    size_t nbytes,
    GstClockTime* prevPtsOut,
    GstClockTime* prevDtsOut) {
    if (!m_adapter || nbytes == 0 || nbytes > available()) {
        if (prevPtsOut) {
            *prevPtsOut = GST_CLOCK_TIME_NONE;
        }
        if (prevDtsOut) {
            *prevDtsOut = GST_CLOCK_TIME_NONE;
        }
        return Buffer();
    }
    if (prevPtsOut) {
        *prevPtsOut = gst_adapter_prev_pts(m_adapter, nullptr);
    }
    if (prevDtsOut) {
        *prevDtsOut = gst_adapter_prev_dts(m_adapter, nullptr);
    }
    return Buffer::adopt(gst_adapter_take_buffer(m_adapter, nbytes));
}

void ByteStreamAdapter::clear() noexcept {
    if (m_adapter) {
        gst_adapter_clear(m_adapter);
    }
}

}  // namespace gps::elem
