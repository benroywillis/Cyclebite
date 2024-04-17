#include "RandomAccessVariable.h"
#include "BasePointer.h"

using namespace std;
using namespace Cyclebite::Grammar;

const shared_ptr<BasePointer>& RandomAccessVariable::getBasePointer() const
{
    return bp;
}