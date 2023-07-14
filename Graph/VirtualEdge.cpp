#include "VirtualEdge.h"
#include "CallEdge.h"
#include "ConditionalEdge.h"
#include "ControlNode.h"
#include <deque>

using namespace TraceAtlas::Graph;
using namespace std;

VirtualEdge::VirtualEdge() : ConditionalEdge()
{
    edges = set<std::shared_ptr<UnconditionalEdge>, GECompare>();
}

VirtualEdge::VirtualEdge(uint64_t frequency, std::shared_ptr<ControlNode> sou, std::shared_ptr<ControlNode> sin, set<std::shared_ptr<UnconditionalEdge>, GECompare> &newEdges) : ConditionalEdge(frequency, sou, sin)
{
    edges.insert(newEdges.begin(), newEdges.end());
    setWeight(frequency);
}

bool VirtualEdge::addEdge(const std::shared_ptr<UnconditionalEdge> newEdge)
{
    return edges.insert(newEdge).second;
}

void VirtualEdge::addEdges(const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> newEdges)
{
    edges.insert(newEdges.begin(), newEdges.end());
}

const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &VirtualEdge::getEdges() const
{
    return edges;
}

bool VirtualEdge::isCallEdge() const
{
    // we only look at the first level of edges below us
    // that is because, after a function has been inlined, or is part of a transform, that call edge has been replaced with an abstraction that does away with the call edge
    // the only time a call edge can be represented here is if this virtual edge was duplicating a real edge, such as in a function inline transform
    if (edges.size() != 1)
        return false;
    if (auto ce = dynamic_pointer_cast<CallEdge>(*edges.begin()))
        return true;
    return false;
}