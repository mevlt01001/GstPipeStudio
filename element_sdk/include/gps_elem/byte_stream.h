#pragma once

#include "gps_elem/buffer.h"

#include <gst/base/gstadapter.h>
#include <gst/gst.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gps::elem {

class ByteStreamAdapter {
public:
    ByteStreamAdapter();
    ~ByteStreamAdapter();

    ByteStreamAdapter(const ByteStreamAdapter&) = delete;
    ByteStreamAdapter& operator=(const ByteStreamAdapter&) = delete;
    ByteStreamAdapter(ByteStreamAdapter&& other) noexcept;
    ByteStreamAdapter& operator=(ByteStreamAdapter&& other) noexcept;

    void push(Buffer buf);
    size_t available() const noexcept;

    std::vector<uint8_t> copyBytes(size_t offset, size_t size) const;
    bool copyBytesInto(void* dest, size_t offset, size_t size) const noexcept;

    gssize maskedScanUint32(uint32_t mask, uint32_t pattern, size_t offset, size_t size) const noexcept;
    void flush(size_t nbytes) noexcept;

    GstClockTime prevPts(uint64_t* distance = nullptr) const noexcept;
    GstClockTime prevDts(uint64_t* distance = nullptr) const noexcept;

    Buffer takeBuffer(
        size_t nbytes,
        GstClockTime* prevPtsOut = nullptr,
        GstClockTime* prevDtsOut = nullptr);

    void clear() noexcept;

    GstAdapter* raw() const noexcept { return m_adapter; }

private:
    GstAdapter* m_adapter = nullptr;
};

}  // namespace gps::elem
