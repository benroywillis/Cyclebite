#include "Multiplexer.h"
#include "Util/Exceptions.h"
#include "Util/Print.h"

using namespace std;
using namespace Cyclebite::Grammar;

Multiplexer::Multiplexer( const std::shared_ptr<Task>& ta, 
                          const std::shared_ptr<Cyclebite::Graph::DataValue>& cond, 
                          const std::vector<std::shared_ptr<Symbol>>& a, 
                          const std::shared_ptr<Symbol>& out ) : OperatorExpression(ta, Cyclebite::Graph::Operation::select, a, out), condition(cond)
{
    // condition must have the same number of outcomes as the number of args
    if( llvm::isa<llvm::SelectInst>(cond->getVal()) || llvm::isa<llvm::CmpInst>(cond->getVal()) )
    {
        if( args.size() != 2 )
        {
            for( const auto& arg : a )
            {
                arg->dump();
                cout << endl;
            }
            PrintVal(cond->getVal());
            throw CyclebiteException("Args do not match the number of conditional outcomes in this multiplexer!");
        }
    }

    // initial class 
}

const shared_ptr<Cyclebite::Graph::DataValue>& Multiplexer::getCondition() const
{
    return condition;
}