#include "CallEdge.h"

using namespace TraceAtlas::Graph;

CallEdge::CallEdge() : ConditionalEdge() {}

CallEdge::CallEdge(const UnconditionalEdge &copy) : ConditionalEdge(copy) {}

CallEdge::CallEdge(uint64_t count, std::shared_ptr<ControlNode> sou, std::shared_ptr<ControlNode> sin) : ConditionalEdge(count, sou, sin) {}