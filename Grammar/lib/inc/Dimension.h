//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <memory>
#include <set>
#include <deque>
#include "Polyhedral.h"
#include "Cycle.h"
#include "Graph/inc/DataValue.h"
#include "Graph/inc/ControlBlock.h"

namespace Cyclebite::Grammar
{
    class Dimension
    {
    public:
        Dimension() = delete;
        virtual ~Dimension();
        const std::shared_ptr<Cyclebite::Graph::DataValue>& getNode() const;
        const std::shared_ptr<Cycle>& getCycle() const;
        bool isOffset(const llvm::Value* v) const;
    protected:
        Dimension( const std::shared_ptr<Cyclebite::Graph::DataValue>& n, const std::shared_ptr<Cycle>& c );
        std::shared_ptr<Cycle> cycle;
        std::shared_ptr<Cyclebite::Graph::DataValue> node;
    };
    // sorts dimensions in hierarchical order (parent-most first, child-most last)
    // in the default stl::set, things are sorted from "least" to "greatest", meaning lhs < rhs being "true" puts lhs before rhs in the set
    // thus, to get parent-most first, lhs is "true" when it is the parent of rhs and false otherwise
    struct DimensionSort
    {
        bool operator()(const std::shared_ptr<Dimension>& lhs, const std::shared_ptr<Dimension>& rhs) const 
        {
            if( lhs == rhs )
            {
                // if lhs is rhs, they are equal, and std::sets take entries whose comparisons are always false to be equal
                return false;
            }
            if( lhs->getCycle()->getChildren().find(rhs->getCycle()) != lhs->getCycle()->getChildren().end() )
            {
                // this is my child, I get sorted first
                return true;
            }
            if( lhs->getCycle()->getParents().empty() && !rhs->getCycle()->getParents().empty() )
            {
                // I have no parent and rhs does, I go first
                return true;
            }
            else 
            {
                std::deque<std::shared_ptr<Cycle>> Q;
                std::set<std::shared_ptr<Cycle>> covered;
                Q.push_front(lhs->getCycle());
                // we assume lhs is the parent of rhs and confirm it with a BFS of the children of the idxTree, starting from lhs
                while( !Q.empty() )
                {
                    for( const auto& c : Q.front()->getChildren() )
                    {
                        if( c->find(rhs->getNode()) )
                        {
                            // confirmed, lhs is rhs's parent
                            return true;
                        }
                        else
                        {
                            if( !covered.contains(c) )
                            {
                                Q.push_back(c);
                                covered.insert(c);
                            }
                        }
                    }
                    Q.pop_front();
                }
            }            
            // if we've made it this far, we have confirmed lhs is NOT the parent of rhs, so return false 
            return false;
        }
    };
} // namespace Cyclebite::Grammar