#include "ConstantSymbol.h"

using namespace TraceAtlas::Grammar;
using namespace std;

string ConstantSymbol::dump() const
{
    return to_string(bits);
}