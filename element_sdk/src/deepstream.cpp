#include "gps_elem/deepstream.h"

#include <utility>

namespace gps::elem {

#ifdef GPS_ELEM_WITH_DEEPSTREAM

namespace {
struct OwnedReadMap {
    Buffer buf;
    Buffer::ReadMap map;
};

struct OwnedWriteMap {
    GstBuffer* raw = nullptr;
    GstMapInfo info{};
    bool mapped = false;
    ~OwnedWriteMap() {
        if (mapped && raw) {
            gst_buffer_unmap(raw, &info);
        }
        if (raw) {
            gst_buffer_unref(raw);
        }
    }
};
}  // namespace

// --- CpuPlaneGuard ---

NvSurfaceBatch::CpuPlaneGuard::CpuPlaneGuard(
    NvBufSurface* surf,
    int index,
    int plane,
    NvBufSurfaceMemMapFlags flags,
    std::shared_ptr<const void> owner)
    : m_owner(std::move(owner)),
      m_surf(surf),
      m_index(index),
      m_plane(plane),
      m_flags(flags) {
    if (m_surf && m_surf->surfaceList && m_index >= 0 &&
        static_cast<uint32_t>(m_index) < m_surf->numFilled &&
        static_cast<uint32_t>(m_index) < m_surf->batchSize &&
        m_plane >= 0 &&
        m_plane < static_cast<int>(m_surf->surfaceList[m_index].planeParams.num_planes)) {
        if (NvBufSurfaceMap(m_surf, m_index, m_plane, m_flags) == 0) {
            if (NvBufSurfaceSyncForCpu(m_surf, m_index, m_plane) == 0) {
                m_mapped = true;
            } else {
                NvBufSurfaceUnMap(m_surf, m_index, m_plane);
            }
        }
    }
    if (!m_mapped) {
        m_surf = nullptr;
        m_owner.reset();
    }
}

NvSurfaceBatch::CpuPlaneGuard::~CpuPlaneGuard() {
    unmap();
}

NvSurfaceBatch::CpuPlaneGuard::CpuPlaneGuard(CpuPlaneGuard&& other) noexcept
    : m_owner(std::move(other.m_owner)),
      m_surf(std::exchange(other.m_surf, nullptr)),
      m_index(other.m_index),
      m_plane(other.m_plane),
      m_flags(other.m_flags),
      m_mapped(std::exchange(other.m_mapped, false)) {}

NvSurfaceBatch::CpuPlaneGuard& NvSurfaceBatch::CpuPlaneGuard::operator=(CpuPlaneGuard&& other) noexcept {
    if (this != &other) {
        unmap();
        m_owner = std::move(other.m_owner);
        m_surf = std::exchange(other.m_surf, nullptr);
        m_index = other.m_index;
        m_plane = other.m_plane;
        m_flags = other.m_flags;
        m_mapped = std::exchange(other.m_mapped, false);
    }
    return *this;
}

void* NvSurfaceBatch::CpuPlaneGuard::data() const noexcept {
    if (!m_mapped || !m_surf || !m_surf->surfaceList) {
        return nullptr;
    }
    return m_surf->surfaceList[m_index].mappedAddr.addr[m_plane];
}

void NvSurfaceBatch::CpuPlaneGuard::unmap() noexcept {
    if (m_mapped && m_surf) {
        if (m_flags != NVBUF_MAP_READ) {
            NvBufSurfaceSyncForDevice(m_surf, m_index, m_plane);
        }
        NvBufSurfaceUnMap(m_surf, m_index, m_plane);
        m_mapped = false;
        m_surf = nullptr;
    }
    m_owner.reset();
}

// --- NvSurfaceBatch ---

NvSurfaceBatch::NvSurfaceBatch(NvSurfaceBatch&& other) noexcept
    : m_mapOwner(std::move(other.m_mapOwner)),
      m_surf(std::exchange(other.m_surf, nullptr)) {}

NvSurfaceBatch& NvSurfaceBatch::operator=(NvSurfaceBatch&& other) noexcept {
    if (this != &other) {
        m_mapOwner = std::move(other.m_mapOwner);
        m_surf = std::exchange(other.m_surf, nullptr);
    }
    return *this;
}

std::optional<NvSurfaceBatch> NvSurfaceBatch::mapRead(const Buffer& buf) {
    if (!buf.isValid()) {
        return std::nullopt;
    }
    auto owned = std::make_shared<OwnedReadMap>();
    owned->buf = buf;
    owned->map = owned->buf.read();
    if (!owned->map.valid() || owned->map.size() < sizeof(NvBufSurface) || !owned->map.data()) {
        return std::nullopt;
    }
    NvSurfaceBatch batch;
    batch.m_surf = reinterpret_cast<NvBufSurface*>(const_cast<uint8_t*>(owned->map.data()));
    batch.m_mapOwner = std::move(owned);
    return batch;
}

std::optional<NvSurfaceBatch> NvSurfaceBatch::mapWrite(Buffer& buf) {
    if (!buf.isValid()) {
        return std::nullopt;
    }
    // Ensure writability on the caller's Buffer BEFORE taking an extra ref for lifetime ownership.
    buf.ensureWritable();
    GstBuffer* raw = buf.get();
    if (!raw) {
        return std::nullopt;
    }
    auto owned = std::make_shared<OwnedWriteMap>();
    owned->raw = gst_buffer_ref(raw);
    if (!gst_buffer_map(owned->raw, &owned->info, GST_MAP_READWRITE)) {
        return std::nullopt;
    }
    owned->mapped = true;
    if (owned->info.size < sizeof(NvBufSurface) || !owned->info.data) {
        return std::nullopt;
    }
    NvSurfaceBatch batch;
    batch.m_surf = reinterpret_cast<NvBufSurface*>(owned->info.data);
    batch.m_mapOwner = std::move(owned);
    return batch;
}

const NvBufSurfaceParams* NvSurfaceBatch::surfaceParams(uint32_t idx) const noexcept {
    if (!m_surf || !m_surf->surfaceList || idx >= m_surf->batchSize) {
        return nullptr;
    }
    return &m_surf->surfaceList[idx];
}

uint32_t NvSurfaceBatch::width(uint32_t idx) const noexcept {
    const auto* p = surfaceParams(idx);
    return p ? p->width : 0;
}

uint32_t NvSurfaceBatch::height(uint32_t idx) const noexcept {
    const auto* p = surfaceParams(idx);
    return p ? p->height : 0;
}

uint32_t NvSurfaceBatch::pitch(uint32_t idx) const noexcept {
    const auto* p = surfaceParams(idx);
    return p ? p->pitch : 0;
}

NvBufSurfaceColorFormat NvSurfaceBatch::colorFormat(uint32_t idx) const noexcept {
    const auto* p = surfaceParams(idx);
    return p ? p->colorFormat : NVBUF_COLOR_FORMAT_INVALID;
}

void* NvSurfaceBatch::dataPtr(uint32_t idx) const noexcept {
    const auto* p = surfaceParams(idx);
    return p ? p->dataPtr : nullptr;
}

NvSurfaceBatch::CpuPlaneGuard NvSurfaceBatch::mapCpuPlane(
    uint32_t idx,
    int plane,
    NvBufSurfaceMemMapFlags flags) const {
    return CpuPlaneGuard(m_surf, static_cast<int>(idx), plane, flags, m_mapOwner);
}

// --- NvBatchMeta ---

std::optional<NvBatchMeta> NvBatchMeta::fromBuffer(const Buffer& buf) {
    if (!buf.isValid()) {
        return std::nullopt;
    }
    NvDsBatchMeta* meta = gst_buffer_get_nvds_batch_meta(buf.get());
    if (!meta) {
        return std::nullopt;
    }
    return NvBatchMeta(meta);
}

std::vector<NvDsFrameMeta*> NvBatchMeta::frames() const {
    std::vector<NvDsFrameMeta*> out;
    if (!m_meta) {
        return out;
    }
    for (NvDsMetaList* l = m_meta->frame_meta_list; l != nullptr; l = l->next) {
        if (l->data) {
            out.push_back(static_cast<NvDsFrameMeta*>(l->data));
        }
    }
    return out;
}

uint32_t NvBatchMeta::objectCount(uint32_t frameIdx) const noexcept {
    if (!m_meta) {
        return 0;
    }
    uint32_t idx = 0;
    for (NvDsMetaList* l = m_meta->frame_meta_list; l != nullptr; l = l->next, ++idx) {
        if (idx == frameIdx && l->data) {
            auto* fmeta = static_cast<NvDsFrameMeta*>(l->data);
            return static_cast<uint32_t>(fmeta->num_obj_meta);
        }
    }
    return 0;
}

std::vector<NvDsObjectMeta*> NvBatchMeta::objects(const NvDsFrameMeta* frame) const {
    std::vector<NvDsObjectMeta*> out;
    if (!frame) {
        return out;
    }
    for (NvDsMetaList* l = frame->obj_meta_list; l != nullptr; l = l->next) {
        if (l->data) {
            out.push_back(static_cast<NvDsObjectMeta*>(l->data));
        }
    }
    return out;
}

#endif  // GPS_ELEM_WITH_DEEPSTREAM

}  // namespace gps::elem
