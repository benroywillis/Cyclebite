//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Export.h"
#include "Task.h"
#include "Expression.h"
#include "InductionVariable.h"
#include "IO.h"
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
    // a parallel cycle is one in which all of its iterations have no dependencies on its other iterations

    // something to track which iteration the inputs of the cycle come from
    // something to track where each atom goes
    // for each input into the expression
    //   where do you come from (this iteration, or a previous iteration)?
    //   where do you go
    // if (there is overlap in what the expression eats and what the expression writes)
    //   figure out which dimension in which that overlap occurs
    //     dimensions (generally) map to cycles, so we can trivially figure out which cycles are parallel and which are not
    //       case: reduction -> I can't parallelize this loop
    //       case: cycle without the expression 
    //             subcase: cycle which doesn't overlap with any input/output -> it is fully parallelizable
    //             subcase: cycle which has overlap with an input/output -> it is not parallelizable
    // else
    //   you are embarassingly parallel
    
    // in order to figure out whether an input "overlaps" with a previous expression output, we need to have a temporal ordering of points in the input/output space
    // thus, we must find out what the stride pattern of the 
    
    // "overlaps" tracks that symbolic overlaps between an input to the task expression, and an output that came from a previous result
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
                }
            }
        }
    }
    return parallelSpots;
}

shared_ptr<Expression> VectorizeExpression( const shared_ptr<Expression>& expr )
{
    shared_ptr<Expression> vecEx = nullptr;



    return vecEx;
}

void Cyclebite::Grammar::Export( const shared_ptr<Expression>& expr )
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
    ParallelizeCycles( expr );
}