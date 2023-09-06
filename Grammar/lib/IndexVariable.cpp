#include "IndexVariable.h"
#include <deque>
#include "IO.h"
#include "Graph/inc/IO.h"
#include "Transforms.h"
#include "Inst.h"
#include "Util/Annotate.h"
#include <llvm/IR/Instructions.h>
#include "Util/Exceptions.h"
#include "Util/Print.h"
#include <spdlog/spdlog.h>

using namespace std;
using namespace Cyclebite::Grammar;

IndexVariable::IndexVariable( const std::shared_ptr<Cyclebite::Graph::DataValue>& n, const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& p = nullptr, const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& c = nullptr ) : Symbol("idx"), node(n), parent(p), child(c) {}

const shared_ptr<Cyclebite::Graph::DataValue>& IndexVariable::getNode() const
{
    return node;
}

const shared_ptr<Cyclebite::Grammar::IndexVariable>& IndexVariable::getParent() const
{
    return parent;
}

const shared_ptr<Cyclebite::Grammar::IndexVariable>& IndexVariable::getChild() const
{
    return child;
}

string IndexVariable::dump() const
{
    return name;
}

const PolySpace IndexVariable::getSpace() const
{
    return space;
}