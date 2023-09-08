#include "ConstantSymbol.h"

using namespace Cyclebite::Grammar;
using namespace std;


template <typename T>
string ConstantSymbol<T>::dump() const
{
    return to_string(bits);
}