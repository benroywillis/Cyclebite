#include "Reduction.h"

using namespace std;
using namespace TraceAtlas::Grammar;

Reduction::Reduction(const shared_ptr<ReductionVariable>& v, const vector<shared_ptr<Symbol>>& s, const vector<TraceAtlas::Graph::Operation>& o ) : Expression(s, o), var(v) {}

string Reduction::dump() const
{
    string expr = name + " " + TraceAtlas::Graph::OperationToString.at(var->getOp()) + "= ";
    if( !symbols.empty() )
    {
        auto b = symbols.begin();
        auto o = ops.begin();
        expr += " "+(*b)->dump();
        b = next(b);
        while( b != symbols.end() )
        {
            expr += " "+string(TraceAtlas::Graph::OperationToString.at(*o))+" "+(*b)->dump();
            b = next(b);
            o = next(o);
        }
    }
    return expr;
}