// boxplacement.h – pixel collision avoidance for newly placed elements and inserted queues.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <QRectF>

namespace gps {
struct Element;
class Graph;
}  // namespace gps

namespace BoxPlacement {

using HeightOf = std::function<double(const std::string& elementName)>;

void placeNew(gps::Graph& g, const std::vector<std::string>& created, const HeightOf& heightOf);
void makeRoomForQueue(gps::Graph& g, const std::string& source, const std::string& queue);

}  // namespace BoxPlacement
