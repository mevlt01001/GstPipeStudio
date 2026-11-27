#pragma once

#include "gps_elem/caps.h"
#include "gps_elem/interface.h"
#include "gps_elem/port.h"
#include "gps_elem/property.h"

#include <gst/base/gstflowcombiner.h>
#include <gst/gst.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gps::elem {

class Element;

struct ElementClassBuilder {
    std::string longName;
    std::string klass = "Generic";
    std::string description;
    std::string author = "GstPipeStudio";

    PropertyRegistry properties;
    std::vector<PortSpec> ports;
    std::vector<InterfaceSpec> interfaces;
    std::shared_ptr<UriHandlerMeta> uriMeta;

    void addInterface(GType ifaceType, GInterfaceInitFunc initFunc, gpointer data = nullptr) {
        interfaces.push_back(InterfaceSpec{ifaceType, initFunc, data});
    }

    void enableUriHandler(GstURIType uriType, std::vector<std::string> protocols) {
        auto meta = std::make_shared<UriHandlerMeta>();
        meta->uriType = uriType;
        meta->protocols = std::move(protocols);
        meta->finalizeProtocols();
        uriMeta = std::move(meta);
    }

    void setMetadata(std::string longName_, std::string klass_, std::string description_, std::string author_ = "GstPipeStudio") {
        longName = std::move(longName_);
        klass = std::move(klass_);
        description = std::move(description_);
        author = std::move(author_);
    }

    void addSinkPort(
        std::string name,
        Caps templateCaps = Caps::any(),
        Presence presence = Presence::Always,
        InputMode mode = InputMode::Direct,
        size_t queueCapacity = 8,
        OverflowPolicy overflow = OverflowPolicy::Block) {
        PortSpec spec;
        spec.name = std::move(name);
        spec.direction = GST_PAD_SINK;
        spec.presence = presence;
        spec.templateCaps = std::move(templateCaps);
        spec.inputMode = mode;
        spec.queueCapacity = queueCapacity;
        spec.overflowPolicy = overflow;
        ports.push_back(std::move(spec));
    }

    void addSrcPort(
        std::string name,
        Caps templateCaps = Caps::any(),
        Presence presence = Presence::Always) {
        PortSpec spec;
        spec.name = std::move(name);
        spec.direction = GST_PAD_SRC;
        spec.presence = presence;
        spec.templateCaps = std::move(templateCaps);
        ports.push_back(std::move(spec));
    }
};

struct ElementClassMeta {
    std::string typeName;
    ElementClassBuilder builder;
    std::function<std::unique_ptr<Element>(GstElement*)> factory;
    gpointer parentClass = nullptr;
};

struct GpsElementInstance {
    GstElement parent;
    Element* impl;
};

struct GpsElementClass {
    GstElementClass parentClass;
    ElementClassMeta* meta;
};

class Element {
public:
    explicit Element(GstElement* raw);
    virtual ~Element();

    Element(const Element&) = delete;
    Element& operator=(const Element&) = delete;

    GstElement* raw() const noexcept { return m_raw; }

    // Lifecycle hooks
    virtual bool open() { return true; }
    virtual bool start() { return true; }
    virtual bool play() { return true; }
    virtual bool pause() { return true; }
    virtual void unblock() {}
    virtual bool stop() { return true; }
    virtual void close() {}

    // Negotiation & events
    virtual bool onSinkCaps(SinkPort& port, const Caps& caps);
    virtual Caps transformCaps(GstPadDirection dir, const Caps& caps, const Caps& filter);
    virtual bool onSinkEvent(SinkPort& port, GstEvent* event);
    virtual bool onSrcEvent(SrcPort& port, GstEvent* event);
    virtual bool onSrcQuery(SrcPort& port, GstQuery* query);

    // Allocation negotiation
    virtual bool proposeAllocation(SinkPort& port, GstQuery* query);
    virtual bool decideAllocation(SrcPort& port, GstQuery* query);

    // Sparse streams, seeking & position/duration queries
    virtual bool onGap(SinkPort& port, GstClockTime pts, GstClockTime duration);
    virtual bool onSeek(
        SrcPort& port,
        double rate,
        GstFormat format,
        GstSeekFlags flags,
        GstSeekType startType,
        gint64 start,
        GstSeekType stopType,
        gint64 stop);
    virtual bool queryDuration(SrcPort& port, GstFormat format, gint64& durationOut);
    virtual bool queryPosition(SrcPort& port, GstFormat format, gint64& positionOut);
    virtual bool querySeeking(
        SrcPort& port,
        GstFormat format,
        gboolean& seekableOut,
        gint64& segmentStartOut,
        gint64& segmentEndOut);

    // URI handler
    virtual std::string currentUri() const { return {}; }
    virtual bool setUri(const std::string& uri, std::string& errorOut);

    // Master clock provider
    void setClockProvider(bool enabled) noexcept;
    bool isClockProvider() const noexcept { return m_clockProvider; }
    virtual GstClock* provideClock() { return nullptr; }

    // Request pads
    virtual SinkPort* onRequestSinkPort(const std::string& name, const Caps* caps);
    virtual SrcPort* onRequestSrcPort(const std::string& name, const Caps* caps);
    virtual void onReleasePort(const std::string& name);

    // Port accessors
    SinkPort* sinkPort(const std::string& name) const;
    SrcPort* srcPort(const std::string& name) const;
    std::vector<SinkPort*> allSinkPorts() const;
    std::vector<SrcPort*> allSrcPorts() const;

    // Sometimes pads
    SrcPort& addSometimesSrcPort(const std::string& name, const Caps& templateCaps, const std::string& templateName = "");
    bool removeSometimesPort(const std::string& name);
    void noMorePads();

    // Internal pad linking
    void linkInternal(Port& a, Port& b);
    GstIterator* iterateInternalLinks(GstPad* pad);

    // Caps queries
    virtual Caps querySinkCaps(SinkPort& port, const Caps& filter);
    virtual bool acceptSinkCaps(SinkPort& port, const Caps& caps);
    virtual Caps querySrcCaps(SrcPort& port, const Caps& filter);

    // Controllable properties
    void syncProperties(GstClockTime timestamp);

    // Roles & state helpers
    void setSinkRole(bool isSink);
    bool isSinkRole() const noexcept { return m_sinkRole; }
    void setLive(bool isLive) noexcept { m_isLive = isLive; }
    bool isLive() const noexcept { return m_isLive; }

    void commitPreroll();
    void postOrDeferEos();
    void clearDeferredEos() noexcept { m_eosDeferred.store(false, std::memory_order_release); }

    // Flow combiner
    GstFlowReturn updateFlowCombiner(GstPad* pad, GstFlowReturn ret);

    // Error reporting
    void reportException(const char* where, const char* what);

    // Internal initialization called by GObject wrapper
    void initAlwaysPorts(const std::vector<PortSpec>& specs);

    // GType registration helper
    static GType registerType(
        const std::string& typeName,
        ElementClassBuilder builder,
        std::function<std::unique_ptr<Element>(GstElement*)> factory);

private:
    static void classInit(gpointer gClass, gpointer classData);
    static void instanceInit(GTypeInstance* instance, gpointer gClass);

    static void trampolineSetProperty(GObject* object, guint propId, const GValue* value, GParamSpec* pspec);
    static void trampolineGetProperty(GObject* object, guint propId, GValue* value, GParamSpec* pspec);
    static void trampolineFinalize(GObject* object);
    static GstStateChangeReturn trampolineChangeState(GstElement* element, GstStateChange transition);
    static GstPad* trampolineRequestNewPad(GstElement* element, GstPadTemplate* templ, const gchar* name, const GstCaps* caps);
    static void trampolineReleasePad(GstElement* element, GstPad* pad);
    static GstClock* trampolineProvideClock(GstElement* element);

    GstStateChangeReturn handleChangeState(GstStateChange transition, GstElementClass* parentClass);

    SinkPort& createSinkPort(const PortSpec& spec, GstPadTemplate* tmpl, const std::string& padName);
    SrcPort& createSrcPort(const PortSpec& spec, GstPadTemplate* tmpl, const std::string& padName);
    std::string generateRequestPadName(const gchar* name, GstPadTemplate* templ);

    GstElement* m_raw = nullptr;
    mutable std::mutex m_flowCombinerMutex;
    GstFlowCombiner* m_flowCombiner = nullptr;

    mutable std::mutex m_portsMutex;
    std::map<std::string, std::unique_ptr<SinkPort>> m_sinkPorts;
    std::map<std::string, std::unique_ptr<SrcPort>> m_srcPorts;
    std::map<GstPad*, std::vector<GstPad*>> m_internalLinks;
    std::atomic<uint32_t> m_linkCookie{0};
    guint32 m_iterCookie = 0;
    std::atomic<uint64_t> m_requestPadCounter{0};

    bool m_sinkRole = false;
    bool m_isLive = false;
    bool m_clockProvider = false;
    std::mutex m_prerollMutex;
    bool m_inReadyToPaused = false;
    bool m_asyncPosted = false;
    std::atomic<bool> m_prerolled{false};
    std::atomic<bool> m_eosDeferred{false};
};

}  // namespace gps::elem
