#include "BasePointer.h"

using namespace Cyclebite::Grammar;
using namespace Cyclebite::Graph;
using namespace std;

const std::shared_ptr<DataNode>& BasePointer::getNode() const
{
    return node;
}

const vector<pair<const llvm::GetElementPtrInst*, const llvm::LoadInst*>>& BasePointer::getAccesses() const
{
    return loads;
}

const vector<pair<const llvm::GetElementPtrInst*, const llvm::StoreInst*>>& BasePointer::getStores() const
{
    return stores;
}

const vector<const llvm::LoadInst*> BasePointer::getlds() const
{
    vector<const llvm::LoadInst*> lds;
    for( const auto& off : loads )
    {
        lds.push_back(off.second);
    }
    return lds;
}

const vector<const llvm::StoreInst*> BasePointer::getsts() const
{
    vector<const llvm::StoreInst*> sts;
    for( const auto& st : stores )
    {
        sts.push_back(st.second);
    }
    return sts;
}

const vector<const llvm::GetElementPtrInst*> BasePointer::getgps() const
{
    vector<const llvm::GetElementPtrInst*> geps;
    for( const auto& off : loads )
    {
        geps.push_back(off.first);
    }
    for( const auto& st : stores )
    {
        geps.push_back(st.first);
    }
    return geps;
}