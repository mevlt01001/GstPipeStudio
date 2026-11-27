#pragma once

#include "gps_elem/buffer.h"

#ifdef GPS_ELEM_WITH_DEEPSTREAM
#include <gstnvdsmeta.h>
#include <nvbufsurface.h>
#endif

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace gps::elem {

#ifdef GPS_ELEM_WITH_DEEPSTREAM

class NvSurfaceBatch {
public:
    class CpuPlaneGuard {
    public:
        CpuPlaneGuard() = default;
        CpuPlaneGuard(
            NvBufSurface* surf,
            int index,
            int plane,
            NvBufSurfaceMemMapFlags flags,
            std::shared_ptr<const void> owner = {});
        ~CpuPlaneGuard();

        CpuPlaneGuard(const CpuPlaneGuard&) = delete;
        CpuPlaneGuard& operator=(const CpuPlaneGuard&) = delete;
        CpuPlaneGuard(CpuPlaneGuard&& other) noexcept;
        CpuPlaneGuard& operator=(CpuPlaneGuard&& other) noexcept;

        bool valid() const noexcept { return m_mapped; }
        void* data() const noexcept;
        void unmap() noexcept;

    private:
        std::shared_ptr<const void> m_owner;
        NvBufSurface* m_surf = nullptr;
        int m_index = 0;
        int m_plane = 0;
        NvBufSurfaceMemMapFlags m_flags = NVBUF_MAP_READ;
        bool m_mapped = false;
    };

    static std::optional<NvSurfaceBatch> mapRead(const Buffer& buf);
    static std::optional<NvSurfaceBatch> mapWrite(Buffer& buf);

    NvSurfaceBatch(const NvSurfaceBatch&) = delete;
    NvSurfaceBatch& operator=(const NvSurfaceBatch&) = delete;
    NvSurfaceBatch(NvSurfaceBatch&& other) noexcept;
    NvSurfaceBatch& operator=(NvSurfaceBatch&& other) noexcept;

    bool valid() const noexcept { return m_surf != nullptr; }
    uint32_t batchSize() const noexcept { return m_surf ? m_surf->batchSize : 0; }
    uint32_t numFilled() const noexcept { return m_surf ? m_surf->numFilled : 0; }
    uint32_t gpuId() const noexcept { return m_surf ? m_surf->gpuId : 0; }
    NvBufSurfaceMemType memType() const noexcept { return m_surf ? m_surf->memType : NVBUF_MEM_DEFAULT; }

    const NvBufSurfaceParams* surfaceParams(uint32_t idx = 0) const noexcept;
    uint32_t width(uint32_t idx = 0) const noexcept;
    uint32_t height(uint32_t idx = 0) const noexcept;
    uint32_t pitch(uint32_t idx = 0) const noexcept;
    NvBufSurfaceColorFormat colorFormat(uint32_t idx = 0) const noexcept;
    void* dataPtr(uint32_t idx = 0) const noexcept;

    CpuPlaneGuard mapCpuPlane(uint32_t idx = 0, int plane = 0, NvBufSurfaceMemMapFlags flags = NVBUF_MAP_READ) const;

    NvBufSurface* raw() const noexcept { return m_surf; }

private:
    NvSurfaceBatch() = default;

    std::shared_ptr<const void> m_mapOwner;
    NvBufSurface* m_surf = nullptr;
};

class NvBatchMeta {
public:
    static std::optional<NvBatchMeta> fromBuffer(const Buffer& buf);

    uint32_t numFrames() const noexcept { return m_meta ? m_meta->num_frames_in_batch : 0; }
    uint32_t maxFrames() const noexcept { return m_meta ? m_meta->max_frames_in_batch : 0; }

    std::vector<NvDsFrameMeta*> frames() const;
    uint32_t objectCount(uint32_t frameIdx = 0) const noexcept;
    std::vector<NvDsObjectMeta*> objects(const NvDsFrameMeta* frame) const;

    NvDsBatchMeta* raw() const noexcept { return m_meta; }

private:
    explicit NvBatchMeta(NvDsBatchMeta* meta) noexcept : m_meta(meta) {}
    NvDsBatchMeta* m_meta = nullptr;
};

#endif  // GPS_ELEM_WITH_DEEPSTREAM

}  // namespace gps::elem
