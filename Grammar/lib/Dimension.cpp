//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Dimension.h"
#include "Util/Exceptions.h"
#include <deque>
#include <llvm/IR/Instructions.h>

using namespace std;
using namespace Cyclebite::Grammar;

Dimension::Dimension( const std::shared_ptr<Cyclebite::Graph::DataValue>& n, const std::shared_ptr<Cycle>& c ) : cycle(c), node(n) {}

Dimension::~Dimension() = default;

const shared_ptr<Cyclebite::Graph::DataValue>& Dimension::getNode() const
{
    return node;
}

const shared_ptr<Cycle>& Dimension::getCycle() const
{
    return cycle;
}

bool Dimension::isOffset(const llvm::Value* v) const
{
    if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(v) )
    {
        deque<const llvm::Value*> Q;
        set<const llvm::Value*> covered;
        Q.push_front(node->getVal());
        covered.insert(node->getVal());
        while( !Q.empty() )
        {
            if( Q.front() == v )
            {
                // this is the value we are looking for, return true
                return true;
            }
            for( const auto& use : Q.front()->users() )
            {
                if( const auto& useInst = llvm::dyn_cast<llvm::Instruction>(use) )
                {
                    if( !covered.contains(useInst) )
                    {
                        Q.push_back(useInst);
                        covered.insert(useInst);
                    }
                }
            }
            Q.pop_front();
        }
    }
    return false;
}