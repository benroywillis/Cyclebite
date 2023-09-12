#include "TaskParameter.h"

using namespace std;
using namespace Cyclebite::Grammar;

const shared_ptr<Cyclebite::Graph::DataValue>& TaskParameter::getNode() const
{
    return node;
}

const shared_ptr<Task>& TaskParameter::getUser() const
{
    return t;
}

string TaskParameter::dump() const
{
    return name;
}