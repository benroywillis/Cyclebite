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

string labelLUT( int noInputs, int noOutputs, vector<int> inputDimensions, vector<int> outputDimensions, bool reduction, int reductionDimensions )
{
    // LUT
    // Task     | # of inputs | # of outputs | # of input dimensions | # of output dimensions | reduction | reduction dimensions | special  |
    // RandInit |      0      |      any     |          any          |           any          |     0     |           -          | "rand()" |
    // ZIP      |      2      |       1      |        any,any        |           any          |     0     |           -          |          |
    // Foreach  |      1      |       1      |          any          |       same as input    |     0     |           -          |          |
    // GEMV     |      2      |       1      |          2,1          |            1           |     1     |           1          |          |
    // GEMM     |      2      |       1      |          2,2          |            2           |     1     |           1          |          |
    // Stencil  |      1      |       1      |           2           |       same as input    |     1     |           2          |          |
    switch( noInputs )
    {
        case 0:
        {
            return "Init";
        }
        case 1:
        {
            return reduction ? "Stencil" : "Foreach";
        }
        case 2: 
        {
            if( reduction )
            {
                if( std::find(inputDimensions.begin(), inputDimensions.end(), 1) != inputDimensions.end() )
                {
                    return "GEMV";
                }
                else
                {
                    return "GEMM";
                }
            }
            else
            {
                return "ZIP";
            }
        }
        default: return "Unknown";
    }
}

string MapTaskToName( const shared_ptr<Expression>& expr )
{
    // measures
    // 1. number of inputs
    // 2. number of outputs
    // 3. number of input dimensions
    // 4. number of output dimensions
    // 5. reduction
    // 6. reduction dimensions
    
    // input dimensions are measured by the max number of dimensions in an input
    vector<int> inDims;
    int inMax = INT_MAX;
    for( const auto& in : expr->getInputs() )
    {
        if( const auto& coll = dynamic_pointer_cast<Collection>(in) )
        {
            inDims.push_back((int)coll->getNumDims());
            inMax = (int)coll->getNumDims() < inMax ? (int)coll->getNumDims() : inMax;
        }
        else
        {
            inDims.push_back(0);
        }
    }
    vector<int> outDims;
    if( const auto& coll = dynamic_pointer_cast<Collection>(expr->getOutput()) )
    {
        outDims.push_back((int)coll->getNumDims());
    }
    else
    {
        outDims.push_back(0);
    }
    int redDims = 0;
    if( const auto& red = dynamic_pointer_cast<Reduction>(expr) )
    {
        // reduction dimension is number of input dimensions - output dimension
        set<shared_ptr<Dimension>> inputDims;
        for( const auto& in : expr->getInputs() )
        {
            if( const auto& coll = dynamic_pointer_cast<Collection>(in) )
            {
                for( const auto& dim : coll->getDimensions() )
                {
                    inputDims.insert(dim);
                }
            }
        }
        redDims = inputDims.size() - (int)*outDims.begin();
    }
    return labelLUT( (int)expr->getInputs().size(), expr->getOutput() ? 1 : 0, inDims, outDims, (bool)redDims, redDims);    
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
    // first, task name
    cout << endl;
    for( const auto& t : taskToExpr )
    {
        spdlog::info("Cyclebite-Template Label: Task"+to_string(t.first->getID())+" -> "+MapTaskToName(t.second));
    }
    cout << endl;
    // second, task optimization and export
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