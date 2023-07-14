#include "ConstantSymbol.h"

using namespace Cyclebite::Grammar;
using namespace std;

string ConstantSymbol::dump() const
{
    return to_string(bits);
}