#include "gps_runtime/pipeline.hpp"

#include <cstdio>
#include <vector>

#define CHECK(cond)                                                               \
    do {                                                                          \
        if (!(cond)) {                                                            \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            return 1;                                                             \
        }                                                                         \
    } while (0)

int main() {
    std::string err;
    auto pipe = gps::runtime::Pipeline::open(MOCK_PIPELINE_PATH, &err);
    CHECK(pipe.has_value());
    CHECK(pipe->manifest().name == "mock_pipeline");
    CHECK(pipe->manifest().params.size() == 1);
    CHECK(pipe->manifest().ports.size() == 2);

    bool sawLog = false;
    pipe->setLogCallback([&](int, const std::string& msg) {
        if (msg == "mock started") sawLog = true;
    });

    std::vector<uint8_t> received;
    pipe->setFrameCallback("src_out", [&](const std::string& port, const gps::runtime::FrameView& f) {
        if (port == "src_out" && f.data && f.size > 0) {
            received.assign(f.data, f.data + f.size);
        }
    });

    CHECK(pipe->start(&err));
    CHECK(sawLog);
    CHECK(pipe->setParam("gain", 2.5, &err));
    CHECK(pipe->getParam("gain").has_value());
    CHECK(!pipe->setParam("nonexistent", 1, &err));

    const uint8_t payload[4] = {10, 20, 30, 40};
    CHECK(pipe->pushFrame("sink_in", payload, sizeof(payload), 1000, "application/x-raw", &err));
    CHECK(received.size() == 4 && received[2] == 30);

    const auto m = pipe->metrics();
    CHECK(m.elements.size() == 1);
    CHECK(m.elements[0].element == "amp_0");

    CHECK(pipe->sendEos());
    CHECK(pipe->waitForEos(1000, &err));
    pipe->stop();
    return 0;
}
