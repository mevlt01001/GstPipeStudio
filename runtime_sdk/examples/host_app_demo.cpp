#include "gps_runtime/pipeline.hpp"

#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path/to/libpipeline.so>\n";
        return 2;
    }
    std::string err;
    auto pipe = gps::runtime::Pipeline::open(argv[1], &err);
    if (!pipe) {
        std::cerr << "Open failed: " << err << "\n";
        return 1;
    }
    std::cout << "Loaded pipeline: " << pipe->manifest().name << " v" << pipe->manifest().version << "\n";
    if (!pipe->start(&err)) {
        std::cerr << "Start failed: " << err << "\n";
        return 1;
    }
    pipe->waitForEos(5000, &err);
    pipe->stop();
    return 0;
}
