#include "ReturnEdge.h"

using namespace TraceAtlas::Graph;
using namespace std;

ReturnEdge::ReturnEdge() : ConditionalEdge() {}

ReturnEdge::ReturnEdge(uint64_t count, shared_ptr<ControlNode> sou, shared_ptr<ControlNode> sin, shared_ptr<CallEdge> call) : ConditionalEdge(count, sou, sin), call(call) {}

const shared_ptr<CallEdge> &ReturnEdge::getCallEdge() const
{
    return call;
}