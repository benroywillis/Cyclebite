#include "DataGraph.h"
#include "UnconditionalEdge.h"

using namespace TraceAtlas::Graph;
using namespace std;

DataGraph::DataGraph() : Graph() {}

DataGraph::DataGraph(const std::set<std::shared_ptr<DataNode>, p_GNCompare> &nodeSet, const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &edgeSet) : Graph(NodeConvert(nodeSet), EdgeConvert(edgeSet)) {}

const set<shared_ptr<DataNode>, p_GNCompare> DataGraph::getDataNodes() const
{
    std::set<std::shared_ptr<DataNode>, p_GNCompare> converted;
    std::transform(nodeSet.begin(), nodeSet.end(), std::inserter(converted, converted.begin()), [](const std::shared_ptr<GraphNode> &down) { return std::static_pointer_cast<DataNode>(down); });
    return converted;
}