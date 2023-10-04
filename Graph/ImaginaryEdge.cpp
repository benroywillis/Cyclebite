// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "ImaginaryEdge.h"
#include "ImaginaryNode.h"
#include "ControlNode.h"

using namespace Cyclebite::Graph;
using namespace std;

ImaginaryEdge::ImaginaryEdge(const shared_ptr<ImaginaryNode>& sou, const shared_ptr<ControlNode>& sin) : GraphEdge(sou, sin) {}

ImaginaryEdge::ImaginaryEdge(const shared_ptr<ControlNode>& sou, const shared_ptr<ImaginaryNode>& sin) : GraphEdge(sou, sin) {}

bool ImaginaryEdge::isEntrance() const
{
    return dynamic_pointer_cast<ImaginaryNode>(src) != nullptr;
}

bool ImaginaryEdge::isExit() const
{
    return dynamic_pointer_cast<ImaginaryNode>(snk) != nullptr;
}