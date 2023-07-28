#include "DataValue.h"

using namespace std;
using namespace Cyclebite::Graph;

DataValue::DataValue(const llvm::Value* val) : v(val) {}

const llvm::Value* DataValue::getVal() const
{
    return v;
}