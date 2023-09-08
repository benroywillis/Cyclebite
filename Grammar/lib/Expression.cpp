#include "Expression.h"
#include "Reduction.h"
#include "Util/Exceptions.h"

using namespace std;
using namespace Cyclebite::Grammar;

Expression::Expression( const vector<shared_ptr<Symbol>>& s, const vector<Cyclebite::Graph::Operation>& o ) : Symbol("expr"), ops(o), symbols(s) 
{
    if( s.empty() )
    {
        throw AtlasException("Expression cannot be empty!");
    }
    else if( o.size() != s.size()-1 )
    {
        throw AtlasException("There should be "+to_string(symbols.size()-1)+" operations for an expression with "+to_string(symbols.size())+" symbols! Operation count: "+to_string(o.size()));
    }
}

string Expression::dump() const
{
    string expr = name + " = ";
    if( !symbols.empty() )
    {
        auto b = symbols.begin();
        auto o = ops.begin();
        expr += " "+(*b)->dump();
        b = next(b);
        while( b != symbols.end() )
        {
            expr += " "+string(Cyclebite::Graph::OperationToString.at(*o))+" "+(*b)->dump();
            b = next(b);
            o = next(o);
        }
    }
    return expr;
}

const vector<shared_ptr<Symbol>>& Expression::getSymbols() const
{
    return symbols;
}

const set<shared_ptr<InductionVariable>>& Expression::getVars() const
{
    return vars;
}

const set<shared_ptr<Collection>> Expression::getCollections() const
{
    set<shared_ptr<Collection>> collections;
    collections.insert(inputs.begin(), inputs.end());
    collections.insert(outputs.begin(), outputs.end());
    return collections;
}

const set<shared_ptr<Collection>>& Expression::getInputs() const
{
    return inputs;
}

const set<shared_ptr<Collection>>& Expression::getOutputs() const
{
    return outputs;
}