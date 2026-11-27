# `gps_element_sdk` v2 Architecture

`gps_element_sdk` is a standalone, pure C++17 object-oriented GStreamer Custom Element Library in `element_sdk/`. Every element registered through this SDK derives **directly** from `GST_TYPE_ELEMENT` (`GstElement` / `GstElementClass`) — never from `GstBaseTransform`, `GstPushSrc`, `GstBaseSink`, or `GstAggregator` — and has zero dependency on Qt or `gstpipestudio/`.

---

## 1. Complete v2 Universal Stream Architecture (8 Modules)

Rather than inheriting from specialized GStreamer base classes whose assumptions break down for multi-input, asynchronous, pull-mode, or dynamic-pad topologies, the SDK models every element by composing **8 orthogonal modules**:

1. **Module 1 — Media & Stream Primitives (`caps.h`, `buffer.h`, `video.h`, `audio.h`)**
   - `Caps`: RAII wrapper over `GstCaps` (`any()`, `empty()`, `custom()`, `videoRaw()`, `audioRaw()`, `intersect()`, `canIntersect()`, `borrow()` / `adopt()`).
   - `Buffer` & `BufferList`: RAII wrappers over `GstBuffer` and `GstBufferList` with copy-on-write `ensureWritable()`, `ReadMap` / `WriteMap`, PTS/DTS/duration/offset/flags accessors, and zero-copy `BufferList` batching.
   - `VideoFrame` / `VideoInfo` (`video.h`) and `AudioFrame` / `AudioInfo` (`audio.h`): RAII frame mapping (`gst_video_frame_map`, `gst_audio_info_from_caps`) and sample/channel/layout helpers.

2. **Module 2 — Byte-Stream & Batching (`byte_stream.h`)**
   - `ByteStreamAdapter`: RAII wrapper over `GstAdapter` with overflow-safe bounds checks (`copyBytes()`, `copyBytesInto()`, `maskedScanUint32()`, `takeBuffer()`, `flush()`, `prevPts()`, `prevDts()`).
   - Paired with `BufferList` (`SrcPort::pushList()`, `SinkPort::DirectListCallback`) for framed packetization and batched buffer delivery.

3. **Module 3 — Metadata & Custom `GstMeta` (`meta.h`)**
   - `CustomMeta<T>`: Type-safe, thread-safe custom `GstMeta` registration and attachment (`attach()`, `get()`, `getMutable()`, `remove()`).
   - Uses FNV-1a hashed type names + process-wide registration mutex for cross-DSO safety, and exception-safe `transformFunc` / `freeFunc` barriers for copy-on-write (`tee`) preservation.

4. **Module 4 — Allocation, Pools & DMA-BUF (`allocation.h`)**
   - `BufferPool`: RAII wrapper over `GstBufferPool` (`configure()`, `setActive()`, `acquire()`).
   - `DmaBufAllocator`: RAII wrapper over `GstDmaBufAllocator` for zero-copy DMA-BUF fd wrapping (`gst_dmabuf_allocator_alloc`) and extraction.
   - Integrates with `SrcPort::negotiateAllocation()`, `Element::decideAllocation()`, and `Element::proposeAllocation()`.

5. **Module 5 — Ports, Scheduling & Flow (`port.h`)**
   - `SinkPort` and `SrcPort` wrap `GstPad*` with owning `gst_object_ref_sink` / `gst_object_unref` lifetime management and `try { ... } catch (...)` exception barriers on all pad trampolines (`chain`, `chain_list`, `getrange`, `event`, `query`, `activatemode`, `iterate_internal_links`).
   - `SinkPort` supports three orthogonal `InputMode`s:
     - `InputMode::Direct`: synchronous callback (`DirectCallback` / `DirectListCallback`) on the upstream streaming thread.
     - `InputMode::Mailbox`: single-slot latest-value store (`latest()`, `takeNew()`, `setDefault()`, `hasReceived()`).
     - `InputMode::BoundedQueue`: thread-safe FIFO (`tryPop()`, `pop()`, `peekPts()`, `popIfHeadBefore()`) with `OverflowPolicy` (`Block`, `DropOldest`, `DropNewest`).
   - `SinkPort` supports `setPullModePreferred(true)` with automatic `PUSH` fallback when upstream does not support `GST_PAD_MODE_PULL`.
   - `SrcPort` manages sticky events (`stream-start`, `caps`, `segment`, `eos`) with generation counters (`m_streamGen`, `m_capsGen`, `m_segmentGen`) and aborts `push()` / `pushList()` / `pushGap()` if sticky event emission fails.

6. **Module 6 — Execution Drivers, Barriers & Clocking (`driver.h`, `barrier.h`, `clock.h`)**
   - `TaskDriver`: wraps `GstTask` + `GRecMutex` + interruptible `waitFor()` for active-thread producers, pull-mode sinks, and multi-input joiners.
   - `BarrierCoordinator`: coordinates N `SinkPort`s in `BoundedQueue` mode under `AllReady` or `PtsWindow` policies, waking on port arrival, EOS, or flush/unblock.
   - `ClockSync` & `SystemClock`: clock waiting (`waitForPts()` with unblock support) and custom clock provider support (`Element::setClockProvider()`, `provideClock()`).

7. **Module 7 — Element Core, Interfaces, Properties & Registration (`element.h`, `interface.h`, `property.h`, `register.h`, `archetypes.h`)**
   - `Element`: translates `GstElement::change_state` into `open()`, `start()`, `pause()`, `play()`, `stop()`, `close()`, `unblock()`, coordinates race-free preroll (`m_prerollMutex`, `m_inReadyToPaused`, `commitPreroll()`), deferred EOS, and mutex-protected `GstFlowCombiner` (`m_flowCombinerMutex` leaf lock).
   - `interface.h` + `ElementClassBuilder`: `GstURIHandler` (`enableUriHandler()`, `currentUri()`, `setUri()`) and `GstTagSetter` support.
   - `property.h`: declarative `PropertyRegistry` bridging C++ fields/lambdas to `GObject` properties.
   - `archetypes.h`: `SourceElement`, `SinkElement`, `TransformElement`, `VideoFilterElement`, `AudioFilterElement`.
   - `register.h`: `ensureElementGType<T>()`, `registerElement<T>()`, `registerElementStatic<T>()`, `GPS_ELEM_DEFINE_PLUGIN(...)`.

8. **Module 8 — DeepStream NVMM & `NvDsBatchMeta` (`deepstream.h`)**
   - `NvSurfaceBatch` & `NvSurfaceBatch::CpuPlaneGuard`: RAII mapping over `NVMM` `NvBufSurface` batches with shared buffer/map ownership (`m_mapOwner`) so `CpuPlaneGuard` outlives temporary `NvSurfaceBatch` objects, bounds-checked plane access, and `NvBufSurfaceSyncForCpu` / `NvBufSurfaceSyncForDevice` symmetry.
   - `NvBatchMeta`: read-only inspection of `NvDsBatchMeta`, `NvDsFrameMeta`, and `NvDsObjectMeta`.

---

## 2. Supported Operations Matrix

| Dimension | Supported Capabilities |
| :--- | :--- |
| **Stream Types** | Video (`VideoInfo`/`VideoFrame`), Audio (`AudioInfo`/`AudioFrame`), Arbitrary byte-streams (`ByteStreamAdapter`), Custom/opaque caps (`Caps::custom`), DeepStream `memory:NVMM` (`NvSurfaceBatch`) |
| **Topologies** | `0 -> 1` (`SourceElement`), `1 -> 0` (`SinkElement`), `1 -> 1` (`TransformElement`, `VideoFilterElement`, `AudioFilterElement`), `1 -> N` (static or `Sometimes`/`Request` splitter), `N -> 1` (`Mailbox` async overlay or `BoundedQueue` + `BarrierCoordinator` join), `M -> N` general graph |
| **Scheduling Modes** | Push (`chain` / `chain_list` + `GstTask` via `TaskDriver`), Pull (`getrange` on `SrcPort`, `pullRange` on `SinkPort`, `setPullModePreferred(true)` with automatic `PUSH` fallback) |
| **Memory Models** | System memory (`Buffer::allocate`), `GstBufferPool` (`decideAllocation` / `proposeAllocation` / `acquireBuffer`), DMA-BUF (`DmaBufAllocator`), DeepStream NVMM (`NvSurfaceBatch` + `CpuPlaneGuard`) |
| **Framing & Batching** | Single `Buffer`, zero-copy `BufferList` (`pushList` / `DirectListCallback`), `ByteStreamAdapter` (`GstAdapter` parsing/framing), `NvBufSurface` batching (`numFilled` / `batchSize`) |
| **Metadata** | Standard PTS/DTS/duration/offset/flags, `CustomMeta<T>` (copy-on-write safe, cross-DSO safe, exception-safe), `NvBatchMeta` (`NvDsBatchMeta` / `NvDsFrameMeta` / `NvDsObjectMeta`) |
| **Events & Queries** | Sticky (`STREAM_START`, `CAPS`, `SEGMENT`, `EOS`), `GAP` (including `SinkElement` preroll), `FLUSH_START` / `FLUSH_STOP`, `SEEK` (`onSrcEvent`), `TAG`, `ALLOCATION`, `CAPS`, `ACCEPT_CAPS`, `SCHEDULING`, `LATENCY`, `DURATION`, `SEEKING`, `POSITION` |
| **GStreamer Interfaces** | `GstURIHandler` (`GST_URI_SRC` / `GST_URI_SINK`), `GstTagSetter`, `GstClock` provider (`GST_ELEMENT_FLAG_PROVIDE_CLOCK`) |

---

## 3. Explicit Architectural Constraints & Boundaries

The following capabilities are **intentionally outside** `gps_element_sdk`:

1. **`GstBin` / Container Composition:** Every SDK element derives directly from `GST_TYPE_ELEMENT`. Sub-pipeline composition, ghost pads, and child element management belong at the application/pipeline layer (`GstPipeline` / `GstBin`), not inside `gps_element_sdk`.
2. **Multi-Process IPC:** Cross-process buffer transport (e.g., `shmsink`/`shmsrc`, Unix socket FD passing, network RTP/RTSP protocols) is out of scope; elements operate within a single GStreamer process.
3. **Custom `GstAllocator` Hardware Drivers:** `element_sdk` wraps existing `GstBufferPool`, `GstDmaBufAllocator`, and DeepStream `NvBufSurface` APIs, but does not implement kernel-level or vendor-specific `GstAllocator` subclasses.
4. **Application / UI Coupling:** `element_sdk` must remain 100% independent of Qt and `gstpipestudio/`.

---

## 4. Build & Test Commands

Run inside the DeepStream development container from the repository root:

```sh
# Non-sanitizer build + full CTest suite (unit tests + gst-inspect + gst-launch)
docker/dev.sh deepstream 'cmake -S element_sdk -B build-elem-v2 -G Ninja -DGPS_ELEM_SANITIZERS=OFF && cmake --build build-elem-v2 -j && ctest --test-dir build-elem-v2 --output-on-failure'

# ASan + UBSan build & test
docker/dev.sh deepstream 'cmake -S element_sdk -B build-elem-v2-asan -G Ninja -DGPS_ELEM_SANITIZERS=ON && cmake --build build-elem-v2-asan -j && ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-elem-v2-asan --output-on-failure'
```
