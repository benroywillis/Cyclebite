#include "DataGraph.h"
#include "UnconditionalEdge.h"

using namespace Cyclebite::Graph;
using namespace std;

DataGraph::DataGraph() : Graph() {}

DataGraph::DataGraph(const std::set<std::shared_ptr<DataValue>, p_GNCompare> &nodeSet, const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &edgeSet) : Graph(NodeConvert(nodeSet), EdgeConvert(edgeSet)) {}
DataGraph::DataGraph(const std::set<std::shared_ptr<Inst>, p_GNCompare> &nodeSet, const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &edgeSet) : Graph(NodeConvert(nodeSet), EdgeConvert(edgeSet)) {}

const set<shared_ptr<DataValue>, p_GNCompare> DataGraph::getDataNodes() const
{
    std::set<std::shared_ptr<DataValue>, p_GNCompare> converted;
    std::transform(nodeSet.begin(), nodeSet.end(), std::inserter(converted, converted.begin()), [](const std::shared_ptr<GraphNode> &down) { return std::static_pointer_cast<DataValue>(down); });
    return converted;
}