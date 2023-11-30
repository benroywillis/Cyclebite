//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "UnconditionalEdge.h"

namespace Cyclebite::Graph
{
    class ImaginaryNode;
    class ControlNode;
    /// @brief Used to signify the entrance and exit of a program
    ///
    /// Imaginary edges can either originate from the void (an entrance to a program) or point to the void (an exit of a program)
    /// In the current paradigm, these edges either point to the first block in main or point from the exit point of the program
    class ImaginaryEdge : public GraphEdge
    {
    public:
        /// Program entrance edge constructor
        ImaginaryEdge(const std::shared_ptr<ImaginaryNode>& sou, const std::shared_ptr<ControlNode>& sin);
        /// Program exit edge constructor
        ImaginaryEdge(const std::shared_ptr<ControlNode>& sou, const std::shared_ptr<ImaginaryNode>& sin);
        /// If the source node is a nullptr, this is an entrance.
        bool isEntrance() const;
        /// If the sink node is a nullptr, this is an exit
        bool isExit() const;
    };
} // namespace Cyclebite::Graph