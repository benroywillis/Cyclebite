#pragma once
#include "ControlNode.h"

namespace TraceAtlas::Graph
{
    class UnconditionalEdge;
    class VirtualNode : public ControlNode
    {
    public:
        VirtualNode();
        ~VirtualNode() = default;
        virtual bool addNode(const std::shared_ptr<ControlNode> &newNode);
        virtual bool addEdge(const std::shared_ptr<UnconditionalEdge>& newEdges );

        virtual void addNodes(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &newNodes);
        virtual void addEdges(const std::set<std::shared_ptr<UnconditionalEdge>, GECompare>& newEdges );
        /// Returns all nodes within the virtual node subgraph
        const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &getSubgraph() const;
        /// Returns all edges whose nodes are within the virtual node subgraph
        const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &getSubgraphEdges() const;

        /// Returns true if search is found within the node subgraph, false otherwise
        bool find(const std::shared_ptr<ControlNode> &search) const;
        std::vector<std::shared_ptr<UnconditionalEdge>> getEntrances() const;
        std::set<uint32_t> getEntranceBlocks(uint32_t markovOrder) const;
        std::vector<std::shared_ptr<UnconditionalEdge>> getExits() const;
        std::set<uint32_t> getExitBlocks(uint32_t markovOrder) const;
        /// Returns the "anchor" block, or block with the highest number of iterations in the MLCycle
        uint64_t getAnchor();

    protected:
        /// Holds the count of the highest iteration node in the subgraph of the virtual node
        uint64_t anchor;
        /// Nodes that sit below this virtual node
        /// Private because it is convenient to vet nodes as they are added to this structure
        std::set<std::shared_ptr<ControlNode>, p_GNCompare> subgraph;
        std::set<std::shared_ptr<UnconditionalEdge>, GECompare> edges;
    };
} // namespace TraceAtlas::Graph