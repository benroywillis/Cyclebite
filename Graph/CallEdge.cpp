// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "CallEdge.h"

using namespace Cyclebite::Graph;

CallEdge::CallEdge() : ConditionalEdge() {}

CallEdge::CallEdge(const UnconditionalEdge &copy) : ConditionalEdge(copy) {}

CallEdge::CallEdge(uint64_t count, std::shared_ptr<ControlNode> sou, std::shared_ptr<ControlNode> sin) : ConditionalEdge(count, sou, sin) {}