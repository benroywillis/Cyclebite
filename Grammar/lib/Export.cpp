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

string labelLUT( int noInputs, int noOutputs, vector<int> inputDimensions, vector<int> outputDimensions, bool reduction, int reductionDimensions, bool inPlace, bool parallel )
{
    // LUT
    // Task     | # of inputs | # of outputs            | # of input dimensions | # of output dimensions | reduction | reduction dimensions | special  |
    // RandInit |      0      |      any                |          any          |           any          |     0     |           -          | "rand()" |
    // ZIP      |      2      |       1                 |        any,any        |           any          |     0     |           -          |          |
    // Map      |      1      |  1 (works out of place) |          any          |       same as input    |     0     |           -          |          |
    // Foreach  |      1      |  0 (worked in-place)    |          any          |       same as input    |     0     |           -          |          |
    // GEMV     |      2      |       1                 |          2,1          |            1           |     1     |           1          |          |
    // GEMM     |      2      |       1                 |          2,2          |            2           |     1     |           1          |          |
    // Stencil  |      1      |       1                 |           2           |       same as input    |     1     |           2          |          |
#if DEBUG
    string inDimString = "";
    if( !inputDimensions.empty() )
    {
        auto it = inputDimensions.begin();
        inDimString += to_string( *it );
        it = next(it);
        while( it != inputDimensions.end() )
        {
            inDimString += ","+to_string(*it);
            it = next(it);
        }
    }
    string outDimString = "";
    if( !outputDimensions.empty() )
    {
        auto it = outputDimensions.begin();
        outDimString += to_string( *it );
        it = next(it);
        while( it != outputDimensions.end() )
        {
            outDimString += ","+to_string(*it);
            it = next(it);
        }
    }
    spdlog::info("# Inputs: "+to_string(noInputs)+
                 "; # Outputs: "+to_string(noOutputs)+
                 "; inDims: "+inDimString+
                 "; outDims: "+outDimString+
                 "; reduction: "+to_string(reduction)+
                 "; redDims: "+to_string(reductionDimensions)+
                 "; inPlace: "+to_string(inPlace));
#endif
    if( parallel )
    {
        switch( noInputs )
        {
            case 0:
            {
                return "Init";
            }
            case 1:
            {
                if( inPlace )
                {
                    return reduction ? "Stencil" : "Foreach";
                }
                else
                {
                    return reduction ? "Stencil" : "Map";
                }
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
            case 4:
            {
                if( reduction && !inPlace && (noOutputs == 1) )
                {
                    return "CGEMM";
                }
                else if( !inPlace && (noOutputs == 1) )
                {
                    return "Map";
                }
                else
                {
                    return "Unknown";
                }
            }
            default: 
            {
                // Map tasks are allowed to have as many inputs as is required
                // since we know this task is parallel, if it doesn't work in-place and produces a single output, it is a map
                if( !inPlace && (noOutputs == 1) )
                {
                    return "Map";
                }
                return "Unknown";
            }
        }
    }
    else
    {
        return "NotParallel";
    }
}

string MapTaskToName( const shared_ptr<Expression>& expr, const set<shared_ptr<Cycle>>& parallelCycles )
{
    // measures
    // 1. number of inputs
    // 2. number of outputs
    // 3. number of input dimensions
    // 4. number of output dimensions
    // 5. reduction
    // 6. reduction dimensions
    
    set<shared_ptr<BasePointer>> inputs;
    for( const auto& in : expr->getInputs() )
    {
        if( const auto& coll = dynamic_pointer_cast<Collection>(in) )
        {
            inputs.insert(coll->getBP());
        }
    }
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
    for( const auto& rv : expr->getRVs() )
    {
        if( redDims < (int)rv->getDimensions().size() )
        {
            redDims = (int)rv->getDimensions().size();
        }
    }
    bool inPlace = false;
    for( const auto& in : expr->getInputs() )
    {
        if( in == expr->getOutput() )
        {
            inPlace = true;
        }
        else
        {
            inPlace = false;
            break;
        }
    }
    return labelLUT( (int)inputs.size(), expr->getOutput() ? 1 : 0, inDims, outDims, (bool)redDims, redDims, inPlace, !parallelCycles.empty() );    
}

set<shared_ptr<Cycle>> ParallelizeCycles( const shared_ptr<Expression>& expr )
{
    // holds cycles whose execution can be done in parallel
    set<shared_ptr<Cycle>> parallelSpots;
    // holds cycles that cannot be executed fully parallel
    set<shared_ptr<Cycle>> noParallel;
    // holds index variables that are common among the input and output
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
    for( const auto& o : overlaps )
    {
        for( const auto& iv : o->getExclusiveDimensions() )
        {
            noParallel.insert(iv->getCycle());
        }
    }
    // second, look for a reduction in the expression
    // this will unlock special optimizations for the algorithm
    shared_ptr<ReductionVariable> rv = nullptr;
    if( expr->hasParallelReduction() )
    {
        for( const auto& rv : expr->getRVs() )
        {
            for( const auto& dim : rv->getDimensions() )
            {
                parallelSpots.insert(dim->getCycle());
            }
        }
    }
    else if( !expr->getRVs().empty() )
    {
        for( const auto& rv : expr->getRVs() )
        {
            for( const auto& dim : rv->getDimensions() )
            {
                noParallel.insert(dim->getCycle());
            }
        }
    }
    // finally, print and return parallel cycles
    for( const auto& c : expr->getTask()->getCycles() )
    {
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
    if( expr->hasParallelReduction() )
    {
        for( const auto& rv : expr->getRVs() )
        {
            for( const auto& dim : rv->getDimensions() )
            {
                reductionCycles.insert(dim->getCycle());
            }
        }
    }
    return reductionCycles;
}

void Cyclebite::Grammar::Export( const map<shared_ptr<Task>, vector<shared_ptr<Expression>>>& taskToExpr )
{
    // first, task name
    cout << endl;
    // second, task optimization and export
    for( const auto& t : taskToExpr )
    {
        for( const auto& expr : t.second )
        {
#ifdef DEBUG
            for( const auto& coll : expr->getCollections() )
            {
                auto dotString = VisualizeCollection(coll);
                ofstream tStream("Task"+to_string(expr->getTask()->getID())+"_Collection"+to_string(coll->getID())+".dot");
                tStream << dotString;
                tStream.close();
            }
#endif
            try
            {
                auto parallelSpots = ParallelizeCycles( expr );
                auto vectorSpots   = VectorizeExpression( expr );
                spdlog::info("Cyclebite-Template Label: Task"+to_string(t.first->getID())+" -> "+MapTaskToName(expr, parallelSpots));
                OMPAnnotateSource(parallelSpots, vectorSpots);
                cout << endl;
            }
            catch( CyclebiteException& e )
            {
                spdlog::critical(e.what());
            }
        }
    }
}