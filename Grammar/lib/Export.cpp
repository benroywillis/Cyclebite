#include "Export.h"
#include "Expression.h"
#include "IO.h"

using namespace std;
using namespace Cyclebite;

void Cyclebite::Grammar::Export( shared_ptr<Expression>& expr )
{
    // first, find out if this task can be profitably parallelized
    // Ben 2023-10-16 for now this just means we try to parallelize the outer-most loop
    // there is a three-step process for this
    // 1. find out the relationships between collections
    // maps a collection to its consumers
    map<shared_ptr<Collection>, set<shared_ptr<Collection>>> prodCons;
    for( const auto& coll : expr->getCollections() )
    {
        
    }
    // 2. find the part that we will parallelize (for now this is just the inner loops, parallelized according to the outer loop)
}