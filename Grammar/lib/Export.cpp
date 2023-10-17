#include "Export.h"
#include "Task.h"
#include "Expression.h"
#include "IO.h"
#include "Collection.h"
#include "Util/Print.h"

using namespace std;
using namespace Cyclebite::Grammar;

bool detectOverlap( const std::shared_ptr<Collection>& coll0, const shared_ptr<Collection>& coll1 )
{

}

void Cyclebite::Grammar::Export( const shared_ptr<Task>& t, const shared_ptr<Expression>& expr )
{
#ifdef DEBUG
    for( const auto& coll : expr->getCollections() )
    {
        auto dotString = VisualizeCollection(coll);
        ofstream tStream("Task"+to_string(t->getID())+"_Collection"+to_string(coll->getID())+".dot");
        tStream << dotString;
        tStream.close();
    }

#endif
    // first, find out if this task can be profitably parallelized
    // Ben 2023-10-16 for now this just means we try to parallelize the outer-most loop
    // there is a three-step process for this
    // 1. find out the relationships between collections
    // maps a collection to its consumers
    map<shared_ptr<Collection>, set<shared_ptr<Collection>>> prodCons;
    for( const auto& coll : expr->getCollections() )
    {
        // we compare the memory region of the collection to the memory regions of all other collections
        for( const auto& coll2 : expr->getCollections() )
        {
            if( coll == coll2 )
            {
                continue;
            }
            if( detectOverlap(coll, coll2) )
            {
                prodCons[coll].insert(coll2);
            }
        }
    }
    // 2. find the part that we will parallelize (for now this is just the inner loops, parallelized according to the outer loop)
}