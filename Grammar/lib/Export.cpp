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

bool overlapsOutputSpace(const shared_ptr<Collection>& input, const shared_ptr<Collection>& output )
{
    // first find the dimension(s) which overlap between input space and output space
    // each overlapping dimension represents a place the input and output space may overlap
    for( const auto& var0 : input->getDimensions() )
    {
        for( const auto& var1 : output->getDimensions() )
        {
            if( var0->overlaps(var1) )
            {
                spdlog::info("Overlapping dimensions are: ");
                spdlog::info(var0->dump());
                spdlog::info(var1->dump());
                // for each overlapping dimension in the input, see whether is modified (by an idxVar) to reference a previously-written output
                // to do this, we search the input dimension for any affine modifiers to it
                shared_ptr<IndexVariable> modifier = nullptr;
                for( const auto& var : input->getIndices() )
                {
                    if( !var->isDimension() )
                    {
                        if( var->getOffsetDimensions().contains(var0) )
                        {
                            // var is a modifier of our overlapping dimension
                            modifier = var;
                        }
                    }
                }
                if( !modifier )
                {
#ifdef DEBUG
                    spdlog::warn("Could not find a modifier for these overlapping dimensions:");
                    spdlog::warn(var0->dump() + " -> "+PrintVal(var0->getNode()->getInst(), false));
                    spdlog::warn(var1->dump() + " -> "+PrintVal(var1->getNode()->getInst(), false));
#endif
                    continue;
                }
                // we then compare that affine modifier to the stride pattern of the output
                if( var1->getSpace().stride > 0 )
                {
                    if( modifier->getOffset().coefficient < 0 )
                    {
                        // we have dipped the input dimension into a previous iteration of the output, this is overlap
                        return true;
                    }
                }
                else if( var1->getSpace().stride < 0 )
                {
                    if( modifier->getOffset().coefficient > 0 )
                    {
                        return true;
                    }
                }
            }
        }
    }
    return false;
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
    set<pair<shared_ptr<Collection>, shared_ptr<Collection>>> overlaps;
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
            if( array == output )
            {
                overlaps.insert( pair(array, output) );
            }
            // we compare the vars of this array with the knowledge we have about the output space and decide whether that input overlaps with a previous output
            else if( overlapsOutputSpace(array, output) )
            {
                overlaps.insert( pair(array, output) );
            }
        }
    }
    for( const auto& overlap : overlaps )
    {
        spdlog::info("Dimensions "+overlap.first->dump()+" and "+overlap.second->dump()+" overlap!");
    }
    if( overlaps.empty() )
    {
        spdlog::info("Task"+to_string(expr->getTask()->getID())+" is fully parallel!");
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