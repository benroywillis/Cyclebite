#pragma once
#include "ControlNode.h"
#include "Graph.h"
#include "MLCycle.h"
#include "llvm/IR/BasicBlock.h"

namespace TraceAtlas::Cartographer
{
    struct StaticLoop
    {
        int id;
        std::set<int64_t> blocks;
        std::set<std::shared_ptr<TraceAtlas::Graph::ControlNode>, TraceAtlas::Graph::p_GNCompare> nodes;
    };

    struct StaticLoopCompare
    {
        bool operator()(const StaticLoop &lhs, const StaticLoop &rhs) const
        {
            return lhs.id < rhs.id;
        }
    };

    std::set<std::shared_ptr<TraceAtlas::Graph::MLCycle>, TraceAtlas::Graph::KCompare> DetectHotCode(const std::set<std::shared_ptr<TraceAtlas::Graph::ControlNode>, TraceAtlas::Graph::p_GNCompare> &nodes, float hotTreshold);
    std::set<std::shared_ptr<TraceAtlas::Graph::MLCycle>, TraceAtlas::Graph::KCompare> DetectHotLoops(const std::set<std::shared_ptr<Graph::MLCycle>, Graph::KCompare> &hotKernels, const Graph::Graph &graph, const std::map<int64_t, llvm::BasicBlock *> &IDToBlock, const std::string &loopfilename);
} // namespace TraceAtlas::Cartographer
