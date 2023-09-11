#include "Collection.h"
#include "IndexVariable.h"
#include "Util/Exceptions.h"
#include <deque>

using namespace Cyclebite::Grammar;
using namespace Cyclebite::Graph;
using namespace std;

Collection::Collection(const std::set<std::shared_ptr<IndexVariable>>& v, const std::shared_ptr<BasePointer>& p ) :  Symbol("collection"), bp(p)
{
    // vars go in hierarchical order (parent-most is the first entry, child-most is the last entry)
    shared_ptr<IndexVariable> parentMost = nullptr;
    for( const auto& idx : v )
    {
        if( idx->getParent() == nullptr )
        {
            if( parentMost )
            {
                throw AtlasException("Found more than one parent-most index variables in this collection!");
            }
            parentMost = idx;
        }
    }
    deque<shared_ptr<IndexVariable>> Q;
    Q.push_front(parentMost);
    while( !Q.empty() )
    {
        vars.push_back(Q.front());
        if( Q.front()->getChild() )
        {
            Q.push_back(Q.front()->getChild());
        }
        Q.pop_front();
    }
}

uint32_t Collection::getNumDims() const
{
    return (uint32_t)vars.size();
}

const shared_ptr<IndexVariable>& Collection::operator[](unsigned i) const
{
    return vars[i];
}

const shared_ptr<BasePointer>& Collection::getBP() const
{
    return bp;
}

const vector<shared_ptr<IndexVariable>>& Collection::getIndices() const
{
    return vars;
}

string Collection::dump() const
{
    string expr = name;
    if( !vars.empty() )
    {
        expr += "( ";
        auto v = vars.begin();
        expr += (*v)->dump();
        v = next(v);
        while( v != vars.end() )
        {
            expr += ", "+(*v)->dump();
            v = next(v);
        }
        expr += " )";
    }
    return expr;
}