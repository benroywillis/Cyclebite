//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Export.h"
#include "Task.h"
#include "Reduction.h"
#include "InductionVariable.h"
#include "IO.h"
#include "Graph/inc/IO.h"
#include "Collection.h"
#include "Util/Print.h"
#include "Util/Exceptions.h"

using namespace std;
using namespace Cyclebite::Grammar;

string MapTaskToName( const shared_ptr<Expression>& expr )
{
    // get polyhedral space for each input collection
    // check to see if 
}

set<shared_ptr<Cycle>> ParallelizeCycles( const shared_ptr<Expression>& expr )
{
    set<shared_ptr<Cycle>> parallelSpots;
    set<shared_ptr<IndexVariable>> overlaps;
    shared_ptr<Collection> output = nullptr;
    if( auto array = dynamic_pointer_cast<Collection>(expr->getOutput()) )
    {
        output = array;
    }
    else
    {
        throw CyclebiteException("Cannot yet handle a task whose output is not a collection!");
    }
    // first, find out which dimensions of the inputs to the expression overlap with the output
    // this will tell us which dimensions cannot be parallelized
    for( const auto& input : expr->getInputs() )
    {
        if( const auto& array = dynamic_pointer_cast<Collection>(input) )
        {
            // compare the input space to the output space to find overlap between the two
            // trivial case, if the input is the output, we have overlap
            auto overlap = array->overlaps(output);
            if( !overlap.empty() )
            {
                spdlog::info("Overlap detected between collections "+array->dump()+" and "+output->dump()+":");
                for( const auto& o : overlap )
                {
                    spdlog::info(o->dump());
                    overlaps.insert(o);
                }
            }
        }
    }
    // second, look for a reduction in the expression
    // this will unlock special optimizations for the algorithm
    shared_ptr<ReductionVariable> rv = nullptr;
    if( const auto& red = dynamic_pointer_cast<Reduction>(expr) )
    {
        rv = red->getRV();
    }
    // third, the parallel spots in the task are all the dimensions that don't overlap and don't reduce
    set<shared_ptr<Cycle>> noParallel;
    for( const auto& o : overlaps )
    {
        for( const auto& iv : o->getExclusiveDimensions() )
        {
            noParallel.insert(iv->getCycle());
        }
    }
    for( const auto& c : expr->getTask()->getCycles() )
    {
        if( rv )
        {
            for( const auto& addr : rv->getAddresses() )
            {
                if( c->find( addr ) )
                {
                    noParallel.insert(c);
                    for( const auto& c : c->getChildren() )
                    {
                        noParallel.insert(c);
                    }
                }
            }
        }
        if( !noParallel.contains(c) )
        {
            string print = "Cycle "+to_string(c->getID())+" ( blocks: ";
            for( const auto& b : c->getBody() )
            {
                print += to_string( b->originalBlocks.front() )+" ";
            }
            print += ") in Task"+to_string(expr->getTask()->getID())+" is parallel!";
            spdlog::info(print);
            parallelSpots.insert(c);
        }
    }
    return parallelSpots;
}

/// @brief Vectorizes reductions
///
/// The inner-most loop of the reduction will be vectorized with "#pragma omp simd"
/// We assume that all reductions, regardless of their underlying data type, to be fully associated.
/// This will result in an error as described in https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html
/// @param red  The reduction to vectorize. Only its inner-most loop will be vectorized with "pragma omp simd"
set<shared_ptr<Cycle>> VectorizeExpression( const shared_ptr<Expression>& expr )
{
    set<shared_ptr<Cycle>> reductionCycles;
    if( const auto& red = dynamic_pointer_cast<Reduction>(expr) )
    {
        if( red->isParallelReduction() )
        {
            reductionCycles.insert(red->getReductionCycle());
        }
    }
    return reductionCycles;
}

void Cyclebite::Grammar::Export( const map<shared_ptr<Task>, shared_ptr<Expression>>& taskToExpr )
{
    for( const auto& t : taskToExpr )
    {
#ifdef DEBUG
        for( const auto& coll : t.second->getCollections() )
        {
            auto dotString = VisualizeCollection(coll);
            ofstream tStream("Task"+to_string(t.second->getTask()->getID())+"_Collection"+to_string(coll->getID())+".dot");
            tStream << dotString;
            tStream.close();
        }
#endif
        try
        {
            auto parallelSpots = ParallelizeCycles( t.second );
            auto vectorSpots   = VectorizeExpression( t.second );
            OMPAnnotateSource(parallelSpots, vectorSpots);
        }
        catch( CyclebiteException& e )
        {
            spdlog::critical(e.what());
        }
    }
}