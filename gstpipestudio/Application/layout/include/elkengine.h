// elkengine.h – layout::Engine on ELK Layered (Eclipse Layout Kernel, elkjs) running inside QuickJS. ELK places boxes in
// columns with as few crossings as it can find, keeps ports where the boxes draw their pads, lays out every frame
// with the same rules, and routes links around boxes as curves or right-angle lines.
// One instance per thread: the JavaScript runtime is not thread safe. The first run loads elkjs (about 1.6 MB).
#pragma once

#include <memory>

#include "layoutgraph.h"

namespace layout {

class ElkEngine final : public Engine {
public:
    ElkEngine();
    ~ElkEngine() override;
    bool run(const Node& root, Routing routing, Result* out, QString* error) override;

private:
    struct Js;
    std::unique_ptr<Js> js_;
};

}  // namespace layout
