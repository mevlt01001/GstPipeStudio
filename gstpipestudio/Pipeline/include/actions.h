#pragma once
#include "pipeline.h"

namespace gps {
struct Placement { std::string element; double x, y; };
class MoveElementsAction final : public IAction {
public:
    explicit MoveElementsAction(std::vector<Placement> positions) : positions_(std::move(positions)) {}
    bool apply(Graph&, std::string*) const override;
private:
    std::vector<Placement> positions_;
};
class DisconnectAction final : public IAction {
public:
    DisconnectAction(std::string element, std::string pad) : element_(std::move(element)), pad_(std::move(pad)) {}
    std::string unavailableReason(const Graph&) const override;
    bool apply(Graph&, std::string*) const override;
private:
    std::string element_, pad_;
};
class InsertFragmentAction final : public IAction {
public:
    using Port = std::pair<std::string,std::string>;
    InsertFragmentAction(GraphFragment fragment, Port target, std::vector<Port> inputs, std::vector<Port> outputs)
        : fragment_(std::move(fragment)), target_(std::move(target)), inputs_(std::move(inputs)), outputs_(std::move(outputs)) {}
    std::string unavailableReason(const Graph&) const override;
    bool apply(Graph&, std::string*) const override;
private:
    GraphFragment fragment_;
    Port target_;
    std::vector<Port> inputs_, outputs_;
};
} // namespace gps
