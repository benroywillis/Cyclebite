#include "Arg.h"

using namespace std;
using namespace Cyclebite::Graph;

const llvm::Argument* Arg::getArg() const
{
    return a;
}