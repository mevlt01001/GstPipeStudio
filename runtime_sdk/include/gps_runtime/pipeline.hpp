#pragma once

#include "gps_runtime/abi.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gps::runtime {

struct ParamInfo {
    std::string name;
    std::string element;
    std::string property;
    GpsRuntimeParamType type = GPS_PARAM_STRING;
    std::string defaultValue;
};

struct PortInfo {
    std::string name;
    GpsRuntimePortDirection direction = GPS_PORT_SINK;
    std::string caps;
};

struct Manifest {
    std::string name;
    std::string version;
    std::vector<ParamInfo> params;
    std::vector<PortInfo> ports;
};

struct ElementMetric {
    std::string element;
    double ageMs = -1.0;
    double queueMs = -1.0;
    double processingMs = -1.0;
    double dropRatePercent = -1.0;
    std::uint64_t inputBuffers = 0;
    std::uint64_t outputBuffers = 0;
};

struct MetricsSnapshot {
    double elapsedMs = 0.0;
    std::vector<ElementMetric> elements;
};

struct FrameView {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::uint64_t ptsNs = 0;
    std::uint64_t durationNs = 0;
    std::string caps;
};

class Pipeline final {
public:
    using FrameCallback = std::function<void(const std::string& port, const FrameView& frame)>;
    using LogCallback = std::function<void(int level, const std::string& message)>;

    Pipeline() = default;
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) noexcept;
    Pipeline& operator=(Pipeline&&) noexcept;

    static std::optional<Pipeline> open(const std::string& soPath, std::string* error = nullptr);

    bool isOpen() const noexcept;
    const Manifest& manifest() const noexcept;

    bool start(std::string* error = nullptr);
    bool pause(std::string* error = nullptr);
    void stop();
    bool sendEos();
    bool waitForEos(std::int64_t timeoutMs = -1, std::string* error = nullptr);

    bool setParam(const std::string& name, const std::string& value, std::string* error = nullptr);
    bool setParam(const std::string& name, const char* value, std::string* error = nullptr) {
        return setParam(name, std::string(value ? value : ""), error);
    }
    bool setParam(const std::string& name, bool value, std::string* error = nullptr);
    bool setParam(const std::string& name, std::int64_t value, std::string* error = nullptr);
    bool setParam(const std::string& name, int value, std::string* error = nullptr) {
        return setParam(name, static_cast<std::int64_t>(value), error);
    }
    bool setParam(const std::string& name, double value, std::string* error = nullptr);
    std::optional<std::string> getParam(const std::string& name) const;

    bool pushFrame(const std::string& port, const std::uint8_t* data, std::size_t size, std::uint64_t ptsNs = 0,
                   const std::string& caps = {}, std::string* error = nullptr);
    void setFrameCallback(const std::string& port, FrameCallback callback);
    void setLogCallback(LogCallback callback);
    MetricsSnapshot metrics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gps::runtime
