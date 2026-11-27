#include "gps_elem/allocation.h"

#include <utility>

namespace gps::elem {

// --- BufferPool ---

BufferPool::~BufferPool() {
    if (m_pool) {
        gst_object_unref(m_pool);
        m_pool = nullptr;
    }
}

BufferPool::BufferPool(const BufferPool& other)
    : m_pool(other.m_pool ? GST_BUFFER_POOL_CAST(gst_object_ref(other.m_pool)) : nullptr) {}

BufferPool& BufferPool::operator=(const BufferPool& other) {
    if (this != &other) {
        if (m_pool) {
            gst_object_unref(m_pool);
        }
        m_pool = other.m_pool ? GST_BUFFER_POOL_CAST(gst_object_ref(other.m_pool)) : nullptr;
    }
    return *this;
}

BufferPool::BufferPool(BufferPool&& other) noexcept
    : m_pool(std::exchange(other.m_pool, nullptr)) {}

BufferPool& BufferPool::operator=(BufferPool&& other) noexcept {
    if (this != &other) {
        if (m_pool) {
            gst_object_unref(m_pool);
        }
        m_pool = std::exchange(other.m_pool, nullptr);
    }
    return *this;
}

BufferPool BufferPool::createSystemPool() {
    return BufferPool(gst_buffer_pool_new());
}

bool BufferPool::isActive() const noexcept {
    return m_pool && gst_buffer_pool_is_active(m_pool) != FALSE;
}

bool BufferPool::configure(const Caps& caps, size_t size, uint32_t minBuffers, uint32_t maxBuffers) {
    if (!m_pool) {
        return false;
    }
    GstStructure* cfg = gst_buffer_pool_get_config(m_pool);
    if (!cfg) {
        return false;
    }
    gst_buffer_pool_config_set_params(
        cfg,
        caps.isValid() ? caps.get() : nullptr,
        static_cast<guint>(size),
        static_cast<guint>(minBuffers),
        static_cast<guint>(maxBuffers));
    return gst_buffer_pool_set_config(m_pool, cfg) != FALSE;
}

bool BufferPool::setActive(bool active) {
    if (!m_pool) {
        return false;
    }
    return gst_buffer_pool_set_active(m_pool, active ? TRUE : FALSE) != FALSE;
}

GstFlowReturn BufferPool::acquire(Buffer& outBuf, GstBufferPoolAcquireParams* params) {
    if (!m_pool) {
        return GST_FLOW_ERROR;
    }
    GstBuffer* raw = nullptr;
    GstFlowReturn ret = gst_buffer_pool_acquire_buffer(m_pool, &raw, params);
    if (ret == GST_FLOW_OK && raw) {
        outBuf = Buffer::adopt(raw);
    }
    return ret;
}

GstBufferPool* BufferPool::release() noexcept {
    return std::exchange(m_pool, nullptr);
}

// --- DmaBufAllocator ---

DmaBufAllocator::~DmaBufAllocator() {
    if (m_alloc) {
        gst_object_unref(m_alloc);
        m_alloc = nullptr;
    }
}

DmaBufAllocator::DmaBufAllocator(const DmaBufAllocator& other)
    : m_alloc(other.m_alloc ? GST_ALLOCATOR_CAST(gst_object_ref(other.m_alloc)) : nullptr) {}

DmaBufAllocator& DmaBufAllocator::operator=(const DmaBufAllocator& other) {
    if (this != &other) {
        if (m_alloc) {
            gst_object_unref(m_alloc);
        }
        m_alloc = other.m_alloc ? GST_ALLOCATOR_CAST(gst_object_ref(other.m_alloc)) : nullptr;
    }
    return *this;
}

DmaBufAllocator::DmaBufAllocator(DmaBufAllocator&& other) noexcept
    : m_alloc(std::exchange(other.m_alloc, nullptr)) {}

DmaBufAllocator& DmaBufAllocator::operator=(DmaBufAllocator&& other) noexcept {
    if (this != &other) {
        if (m_alloc) {
            gst_object_unref(m_alloc);
        }
        m_alloc = std::exchange(other.m_alloc, nullptr);
    }
    return *this;
}

DmaBufAllocator DmaBufAllocator::create() {
    return DmaBufAllocator(gst_dmabuf_allocator_new());
}

Buffer DmaBufAllocator::alloc(int fd, size_t size, bool closeOnUnref) {
    if (!m_alloc || fd < 0 || size == 0) {
        return Buffer();
    }
    GstFdMemoryFlags flags = closeOnUnref ? GST_FD_MEMORY_FLAG_NONE : GST_FD_MEMORY_FLAG_DONT_CLOSE;
    GstMemory* mem = gst_fd_allocator_alloc(m_alloc, fd, size, flags);
    if (!mem) {
        return Buffer();
    }
    GstBuffer* buf = gst_buffer_new();
    gst_buffer_append_memory(buf, mem);
    return Buffer::adopt(buf);
}

bool DmaBufAllocator::isDmaBuf(const Buffer& buf) noexcept {
    if (!buf.isValid() || gst_buffer_n_memory(buf.get()) == 0) {
        return false;
    }
    GstMemory* mem = gst_buffer_peek_memory(buf.get(), 0);
    return mem && gst_is_dmabuf_memory(mem) != FALSE;
}

int DmaBufAllocator::fd(const Buffer& buf) noexcept {
    if (!isDmaBuf(buf)) {
        return -1;
    }
    GstMemory* mem = gst_buffer_peek_memory(buf.get(), 0);
    return gst_dmabuf_memory_get_fd(mem);
}

}  // namespace gps::elem
