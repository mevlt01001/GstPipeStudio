#pragma once

#include "gps_elem/allocation.h"
#include "gps_elem/buffer.h"
#include "gps_elem/caps.h"

#include <gst/gst.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

namespace gps::elem {

class Element;

enum class Presence {
    Always,
    Request,
    Sometimes
};

enum class InputMode {
    Direct,
    Mailbox,
    BoundedQueue
};

enum class OverflowPolicy {
    Block,
    DropOldest,
    DropNewest
};

struct PortSpec {
    std::string name;
    GstPadDirection direction = GST_PAD_UNKNOWN;
    Presence presence = Presence::Always;
    Caps templateCaps = Caps::any();
    InputMode inputMode = InputMode::Direct;
    size_t queueCapacity = 8;
    OverflowPolicy overflowPolicy = OverflowPolicy::Block;
};

class Port {
public:
    virtual ~Port() = default;

    const std::string& name() const noexcept { return m_name; }
    GstPadDirection direction() const noexcept { return m_direction; }
    Presence presence() const noexcept { return m_presence; }
    const Caps& templateCaps() const noexcept { return m_templateCaps; }
    GstPad* pad() const noexcept { return m_pad; }
    Element* element() const noexcept { return m_element; }

    bool isLinked() const noexcept { return m_pad && gst_pad_is_linked(m_pad); }

protected:
    Port(Element* element, GstPad* pad, PortSpec spec);

    Element* m_element = nullptr;
    GstPad* m_pad = nullptr;
    std::string m_name;
    GstPadDirection m_direction = GST_PAD_UNKNOWN;
    Presence m_presence = Presence::Always;
    Caps m_templateCaps;
};

class SinkPort : public Port {
public:
    using DirectCallback = std::function<GstFlowReturn(Buffer)>;
    using DirectListCallback = std::function<GstFlowReturn(BufferList)>;

    SinkPort(Element* element, GstPad* pad, PortSpec spec);
    ~SinkPort() override;

    InputMode inputMode() const noexcept { return m_inputMode; }

    // Direct mode
    void setDirectCallback(DirectCallback cb);
    void setDirectListCallback(DirectListCallback cb);

    // Pull mode
    void setPullModePreferred(bool preferred) noexcept {
        m_pullModePreferred.store(preferred, std::memory_order_release);
    }
    bool isPullModePreferred() const noexcept {
        return m_pullModePreferred.load(std::memory_order_acquire);
    }
    bool isPullModeActive() const noexcept { return m_pullActive.load(std::memory_order_acquire); }
    GstFlowReturn pullRange(uint64_t offset, uint32_t size, Buffer& outBuf);

    // Mailbox mode
    void setDefault(Buffer buf);
    std::optional<Buffer> latest();
    std::optional<Buffer> takeNew();
    bool hasReceived() const noexcept;
    bool waitForFirst(GstClockTime timeoutNs = GST_CLOCK_TIME_NONE);

    // BoundedQueue mode
    std::optional<Buffer> tryPop();
    std::optional<Buffer> pop(GstClockTime timeoutNs = GST_CLOCK_TIME_NONE);
    std::optional<GstClockTime> peekPts();
    bool popIfHeadBefore(GstClockTime cutoffPts);
    bool isQueueEmpty();
    size_t queueSize();
    void clearQueue();

    // Wait helper for BarrierCoordinator
    using WakeCallback = std::function<void()>;
    void setWakeCallback(WakeCallback cb);

    template <typename Pred>
    bool waitUntil(Pred&& pred, GstClockTime timeoutNs = GST_CLOCK_TIME_NONE) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (timeoutNs == GST_CLOCK_TIME_NONE) {
            m_cv.wait(lock, std::forward<Pred>(pred));
            return true;
        }
        return m_cv.wait_for(lock, std::chrono::nanoseconds(timeoutNs), std::forward<Pred>(pred));
    }

    std::mutex& mutex() noexcept { return m_mutex; }
    std::condition_variable& condvar() noexcept { return m_cv; }

    // State & metadata
    std::optional<Caps> currentCaps() const;
    GstSegment segment() const;
    bool isFlushing() const noexcept { return m_flushing.load(std::memory_order_acquire); }
    bool isEos() const noexcept { return m_eos.load(std::memory_order_acquire); }
    bool isUnblocked() const noexcept { return m_unblocked.load(std::memory_order_acquire); }
    uint64_t droppedCount() const noexcept { return m_droppedCount.load(std::memory_order_acquire); }

    void unblock();
    void resetUnblock();
    void resetState();

    // Called by C trampolines
    GstFlowReturn handleChain(GstBuffer* buf);
    GstFlowReturn handleChainList(GstBufferList* list);
    gboolean handleEvent(GstEvent* event);
    gboolean handleQuery(GstQuery* query);
    gboolean handleActivate(GstObject* parent);
    gboolean handleActivateMode(GstPadMode mode, gboolean active);

private:
    static GstFlowReturn trampolineChain(GstPad* pad, GstObject* parent, GstBuffer* buf);
    static GstFlowReturn trampolineChainList(GstPad* pad, GstObject* parent, GstBufferList* list);
    static gboolean trampolineEvent(GstPad* pad, GstObject* parent, GstEvent* event);
    static gboolean trampolineQuery(GstPad* pad, GstObject* parent, GstQuery* query);
    static gboolean trampolineActivate(GstPad* pad, GstObject* parent);
    static gboolean trampolineActivateMode(GstPad* pad, GstObject* parent, GstPadMode mode, gboolean active);
    static GstIterator* trampolineIterateInternalLinks(GstPad* pad, GstObject* parent);

    InputMode m_inputMode = InputMode::Direct;
    size_t m_queueCapacity = 8;
    OverflowPolicy m_overflowPolicy = OverflowPolicy::Block;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;

    DirectCallback m_directCb;
    DirectListCallback m_directListCb;
    WakeCallback m_wakeCb;
    std::atomic<bool> m_pullModePreferred{false};
    std::atomic<bool> m_pullActive{false};

    // Mailbox state
    std::optional<Buffer> m_defaultMailbox;
    std::optional<Buffer> m_latestMailbox;
    bool m_hasReceived = false;
    bool m_hasNewMailbox = false;

    // BoundedQueue state
    std::deque<Buffer> m_queue;

    // Stream state
    Caps m_currentCaps;
    GstSegment m_segment{};
    bool m_hasSegment = false;

    std::atomic<bool> m_flushing{false};
    std::atomic<bool> m_eos{false};
    std::atomic<bool> m_unblocked{false};
    std::atomic<uint64_t> m_droppedCount{0};
};

class SrcPort : public Port {
public:
    using GetRangeCallback = std::function<GstFlowReturn(uint64_t offset, uint32_t length, Buffer& outBuf)>;

    SrcPort(Element* element, GstPad* pad, PortSpec spec);
    ~SrcPort() override;

    void setStreamId(std::string streamId);
    void setCaps(const Caps& caps);
    void setSegment(const GstSegment& segment);
    void resetStickyState(bool keepStreamStart = true);
    void resetSegmentState();

    std::optional<Caps> currentCaps() const;

    GstFlowReturn push(Buffer buf);
    GstFlowReturn pushList(BufferList list);
    bool pushGap(GstClockTime pts, GstClockTime duration = GST_CLOCK_TIME_NONE);
    bool pushTags(GstTagList* tags);
    bool pushEos();
    bool pushEvent(GstEvent* event);

    // Allocation negotiation & pool
    bool negotiateAllocation();
    void setPool(BufferPool pool);
    BufferPool pool() const;
    Buffer acquireBuffer(size_t fallbackSize);

    // Pull mode
    void setGetRangeCallback(GetRangeCallback cb);

    // Called by C trampolines
    gboolean handleEvent(GstEvent* event);
    gboolean handleQuery(GstQuery* query);
    GstFlowReturn handleGetRange(uint64_t offset, uint32_t length, GstBuffer** buffer);
    gboolean handleActivateMode(GstPadMode mode, gboolean active);

private:
    bool ensureStickyEvents();

    static gboolean trampolineEvent(GstPad* pad, GstObject* parent, GstEvent* event);
    static gboolean trampolineQuery(GstPad* pad, GstObject* parent, GstQuery* query);
    static GstFlowReturn trampolineGetRange(GstPad* pad, GstObject* parent, guint64 offset, guint length, GstBuffer** buffer);
    static gboolean trampolineActivateMode(GstPad* pad, GstObject* parent, GstPadMode mode, gboolean active);
    static GstIterator* trampolineIterateInternalLinks(GstPad* pad, GstObject* parent);

    mutable std::mutex m_pushMutex;
    mutable std::mutex m_mutex;
    std::string m_streamId;
    Caps m_pendingCaps;
    GstSegment m_segment{};
    bool m_hasSegment = false;
    BufferPool m_pool;
    std::atomic<bool> m_acceptingPool{true};
    GetRangeCallback m_getRangeCb;

    bool m_streamStartSent = false;
    bool m_capsSent = false;
    bool m_segmentSent = false;
    uint64_t m_streamGen = 0;
    uint64_t m_capsGen = 0;
    uint64_t m_segmentGen = 0;
    std::atomic<bool> m_eosSent{false};
};

}  // namespace gps::elem
