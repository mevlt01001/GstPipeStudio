#pragma once

#include "gps_elem/buffer.h"
#include "gps_elem/caps.h"

#include <gst/allocators/gstdmabuf.h>
#include <gst/gst.h>

#include <cstddef>
#include <cstdint>

namespace gps::elem {

class BufferPool {
public:
    BufferPool() = default;
    ~BufferPool();

    BufferPool(const BufferPool& other);
    BufferPool& operator=(const BufferPool& other);
    BufferPool(BufferPool&& other) noexcept;
    BufferPool& operator=(BufferPool&& other) noexcept;

    static BufferPool adopt(GstBufferPool* pool) noexcept { return BufferPool(pool); }
    static BufferPool borrow(GstBufferPool* pool) noexcept {
        return BufferPool(pool ? GST_BUFFER_POOL_CAST(gst_object_ref(pool)) : nullptr);
    }
    static BufferPool createSystemPool();

    bool isValid() const noexcept { return m_pool != nullptr; }
    bool isActive() const noexcept;

    bool configure(const Caps& caps, size_t size, uint32_t minBuffers = 2, uint32_t maxBuffers = 0);
    bool setActive(bool active);
    GstFlowReturn acquire(Buffer& outBuf, GstBufferPoolAcquireParams* params = nullptr);

    GstBufferPool* raw() const noexcept { return m_pool; }
    GstBufferPool* release() noexcept;

private:
    explicit BufferPool(GstBufferPool* pool) noexcept : m_pool(pool) {}
    GstBufferPool* m_pool = nullptr;
};

class DmaBufAllocator {
public:
    DmaBufAllocator() = default;
    ~DmaBufAllocator();

    DmaBufAllocator(const DmaBufAllocator& other);
    DmaBufAllocator& operator=(const DmaBufAllocator& other);
    DmaBufAllocator(DmaBufAllocator&& other) noexcept;
    DmaBufAllocator& operator=(DmaBufAllocator&& other) noexcept;

    static DmaBufAllocator create();

    bool isValid() const noexcept { return m_alloc != nullptr; }
    Buffer alloc(int fd, size_t size, bool closeOnUnref = true);

    static bool isDmaBuf(const Buffer& buf) noexcept;
    static int fd(const Buffer& buf) noexcept;

    GstAllocator* raw() const noexcept { return m_alloc; }

private:
    explicit DmaBufAllocator(GstAllocator* alloc) noexcept : m_alloc(alloc) {}
    GstAllocator* m_alloc = nullptr;
};

}  // namespace gps::elem
