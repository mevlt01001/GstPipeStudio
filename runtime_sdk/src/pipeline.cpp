#include "gps_runtime/pipeline.hpp"

#include <dlfcn.h>

#include <map>
#include <mutex>
#include <utility>

namespace gps::runtime {

struct Pipeline::Impl {
    void* dlHandle = nullptr;
    const GpsPipelineModuleV1* mod = nullptr;
    GpsPipelineHandle* instance = nullptr;
    Manifest manifest;

    std::mutex cbMutex;
    std::map<std::string, FrameCallback> frameCallbacks;
    LogCallback logCallback;

    ~Impl() {
        if (instance && mod && mod->destroy) {
            mod->destroy(instance);
            instance = nullptr;
        }
        if (dlHandle) {
            dlclose(dlHandle);
            dlHandle = nullptr;
        }
    }

    static void onFrame(const char* port, const GpsRuntimeFrame* frame, void* userData) {
        if (!userData || !port || !frame) return;
        auto* self = static_cast<Impl*>(userData);
        FrameCallback cb;
        {
            std::lock_guard<std::mutex> lock(self->cbMutex);
            auto it = self->frameCallbacks.find(port);
            if (it != self->frameCallbacks.end()) cb = it->second;
        }
        if (cb) {
            FrameView view;
            view.data = frame->data;
            view.size = frame->size;
            view.ptsNs = frame->pts_ns;
            view.durationNs = frame->duration_ns;
            if (frame->caps) view.caps = frame->caps;
            cb(port, view);
        }
    }

    static void onLog(int level, const char* message, void* userData) {
        if (!userData || !message) return;
        auto* self = static_cast<Impl*>(userData);
        LogCallback cb;
        {
            std::lock_guard<std::mutex> lock(self->cbMutex);
            cb = self->logCallback;
        }
        if (cb) cb(level, message);
    }
};

Pipeline::~Pipeline() = default;
Pipeline::Pipeline(Pipeline&&) noexcept = default;
Pipeline& Pipeline::operator=(Pipeline&&) noexcept = default;

std::optional<Pipeline> Pipeline::open(const std::string& soPath, std::string* error) {
    auto impl = std::make_unique<Impl>();
    dlerror();
    impl->dlHandle = dlopen(soPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!impl->dlHandle) {
        if (error) {
            const char* msg = dlerror();
            *error = msg ? msg : ("dlopen failed for " + soPath);
        }
        return std::nullopt;
    }

    dlerror();
    auto fn = reinterpret_cast<GpsPipelineGetModuleV1Fn>(dlsym(impl->dlHandle, GPS_PIPELINE_MODULE_SYMBOL));
    if (!fn) {
        if (error) {
            const char* msg = dlerror();
            *error = msg ? msg : (std::string("missing symbol ") + GPS_PIPELINE_MODULE_SYMBOL);
        }
        return std::nullopt;
    }

    const GpsPipelineModuleV1* mod = fn();
    if (!mod || mod->abi_version != GPS_PIPELINE_ABI_VERSION || !mod->create || !mod->destroy) {
        if (error) *error = "incompatible or invalid GpsPipelineModuleV1 ABI";
        return std::nullopt;
    }
    impl->mod = mod;

    char errBuf[512] = {};
    impl->instance = mod->create(errBuf, sizeof(errBuf));
    if (!impl->instance) {
        if (error) *error = errBuf[0] ? std::string(errBuf) : "module create() failed";
        return std::nullopt;
    }

    if (mod->pipeline_name) impl->manifest.name = mod->pipeline_name;
    if (mod->pipeline_version) impl->manifest.version = mod->pipeline_version;
    for (size_t i = 0; i < mod->param_count && mod->params; ++i) {
        const auto& p = mod->params[i];
        impl->manifest.params.push_back({p.name ? p.name : "", p.element ? p.element : "", p.property ? p.property : "",
                                         p.type, p.default_value ? p.default_value : ""});
    }
    for (size_t i = 0; i < mod->port_count && mod->ports; ++i) {
        const auto& port = mod->ports[i];
        impl->manifest.ports.push_back({port.name ? port.name : "", port.direction, port.caps ? port.caps : ""});
    }

    Pipeline out;
    out.impl_ = std::move(impl);
    return out;
}

bool Pipeline::isOpen() const noexcept { return impl_ && impl_->instance && impl_->mod; }

const Manifest& Pipeline::manifest() const noexcept {
    static const Manifest empty;
    return impl_ ? impl_->manifest : empty;
}

bool Pipeline::start(std::string* error) {
    if (!isOpen() || !impl_->mod->start) return false;
    char err[512] = {};
    const int ok = impl_->mod->start(impl_->instance, err, sizeof(err));
    if (!ok && error && err[0]) *error = err;
    return ok != 0;
}

bool Pipeline::pause(std::string* error) {
    if (!isOpen() || !impl_->mod->pause) return false;
    char err[512] = {};
    const int ok = impl_->mod->pause(impl_->instance, err, sizeof(err));
    if (!ok && error && err[0]) *error = err;
    return ok != 0;
}

void Pipeline::stop() {
    if (isOpen() && impl_->mod->stop) impl_->mod->stop(impl_->instance);
}

bool Pipeline::sendEos() {
    if (!isOpen() || !impl_->mod->send_eos) return false;
    return impl_->mod->send_eos(impl_->instance) != 0;
}

bool Pipeline::waitForEos(std::int64_t timeoutMs, std::string* error) {
    if (!isOpen() || !impl_->mod->wait_for_eos) return false;
    char err[512] = {};
    const int ok = impl_->mod->wait_for_eos(impl_->instance, timeoutMs, err, sizeof(err));
    if (!ok && error && err[0]) *error = err;
    return ok != 0;
}

bool Pipeline::setParam(const std::string& name, const std::string& value, std::string* error) {
    if (!isOpen() || !impl_->mod->set_param) return false;
    char err[512] = {};
    const int ok = impl_->mod->set_param(impl_->instance, name.c_str(), value.c_str(), err, sizeof(err));
    if (!ok && error && err[0]) *error = err;
    return ok != 0;
}

bool Pipeline::setParam(const std::string& name, bool value, std::string* error) {
    return setParam(name, std::string(value ? "true" : "false"), error);
}

bool Pipeline::setParam(const std::string& name, std::int64_t value, std::string* error) {
    return setParam(name, std::to_string(value), error);
}

bool Pipeline::setParam(const std::string& name, double value, std::string* error) {
    return setParam(name, std::to_string(value), error);
}

std::optional<std::string> Pipeline::getParam(const std::string& name) const {
    if (!isOpen() || !impl_->mod->get_param) return std::nullopt;
    char out[512] = {};
    if (!impl_->mod->get_param(impl_->instance, name.c_str(), out, sizeof(out))) return std::nullopt;
    return std::string(out);
}

bool Pipeline::pushFrame(const std::string& port, const std::uint8_t* data, std::size_t size, std::uint64_t ptsNs,
                         const std::string& caps, std::string* error) {
    if (!isOpen() || !impl_->mod->push_frame) return false;
    char err[512] = {};
    const int ok = impl_->mod->push_frame(impl_->instance, port.c_str(), data, size, ptsNs,
                                          caps.empty() ? nullptr : caps.c_str(), err, sizeof(err));
    if (!ok && error && err[0]) *error = err;
    return ok != 0;
}

void Pipeline::setFrameCallback(const std::string& port, FrameCallback callback) {
    if (!isOpen() || !impl_->mod->set_frame_callback) return;
    {
        std::lock_guard<std::mutex> lock(impl_->cbMutex);
        impl_->frameCallbacks[port] = std::move(callback);
    }
    impl_->mod->set_frame_callback(impl_->instance, port.c_str(), &Impl::onFrame, impl_.get());
}

void Pipeline::setLogCallback(LogCallback callback) {
    if (!isOpen() || !impl_->mod->set_log_callback) return;
    {
        std::lock_guard<std::mutex> lock(impl_->cbMutex);
        impl_->logCallback = std::move(callback);
    }
    impl_->mod->set_log_callback(impl_->instance, &Impl::onLog, impl_.get());
}

MetricsSnapshot Pipeline::metrics() const {
    MetricsSnapshot snap;
    if (!isOpen() || !impl_->mod->get_metrics) return snap;
    GpsRuntimeMetrics raw{};
    if (!impl_->mod->get_metrics(impl_->instance, &raw)) return snap;
    snap.elapsedMs = raw.elapsed_ms;
    for (size_t i = 0; i < raw.element_count && raw.elements; ++i) {
        const auto& e = raw.elements[i];
        snap.elements.push_back({e.element ? e.element : "", e.age_ms, e.queue_ms, e.processing_ms,
                                 e.drop_rate_percent, e.input_buffers, e.output_buffers});
    }
    return snap;
}

}  // namespace gps::runtime
