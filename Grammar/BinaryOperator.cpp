#include "BinaryOperator.h"

using namespace Cyclebite::Grammar;
using namespace Cyclebite::Graph;
using namespace std;

BinaryOperator::BinaryOperator(const vector<shared_ptr<Symbol>>& vec, const Operation& o) : op(o), Expression(vec) {}

string BinaryOperator::dump() const
{
    return OperationToString.at(op);
}