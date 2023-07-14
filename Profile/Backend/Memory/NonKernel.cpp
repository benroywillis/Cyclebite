#include "NonKernel.h"

using namespace std;
using namespace TraceAtlas::Profile::Backend::Memory;

NonKernel::NonKernel(pair<int64_t, int64_t> ent) : CodeSection(ent) 
{
    blocks.insert(ent.second);
    contextLevel = 0;
}