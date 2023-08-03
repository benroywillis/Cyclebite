#include "Util/Exceptions.h"
#include "Util/Annotate.h"
#include "Util/Print.h"
#include "BasePointer.h"
#include "IO.h"
#include "Graph/inc/IO.h"
#include <deque>

using namespace Cyclebite::Grammar;
using namespace Cyclebite::Graph;
using namespace std;

const std::shared_ptr<DataValue>& BasePointer::getNode() const
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

/// @brief Determines if the current function is an allocating function
///
/// 
/// @param call 
/// @return 0 if either the function is not an allocation, or the allocation size is not sufficient to be considered a base pointer. Otherwise return the allocation size. 
uint32_t Cyclebite::Grammar::isAllocatingFunction(const llvm::CallBase* call)
{
    if( call->getCalledFunction() )
    {
        if( (call->getCalledFunction()->getName() == "malloc") || (call->getCalledFunction()->getName() == "_Znam") || (call->getCalledFunction()->getName() == "_Znwm") )
        {
            // return its allocation size
            // the functions identified above have a single argument - their allocation in size
            // if it is determinable, we return that value
            if( call->getNumArgOperands() == 1 )
            {
                if( const auto& con = llvm::dyn_cast<llvm::Constant>(call->getArgOperand(0)) )
                {
                    return (uint32_t)*con->getUniqueInteger().getRawData();
                }
            }
            else
            {
                throw AtlasException("Cannot determine allocator function size argument!");
            }
            // if it is not, we investigate the ld/st instructions that touch it to see if they are significant memory instructions. If it has at least one, we return the minimum threshold, if it doesn't we return 0
            std::deque<const llvm::Value*> Q;
            std::set<const llvm::Value*>   covered;
            std::set<const llvm::Value*>   ldsnsts;
            Q.push_front(call);
            covered.insert(call);
            while( !Q.empty() )
            {
                if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(Q.front()) )
                {
                    if( DNIDMap.find(inst) == DNIDMap.end() )
                    {
                        Q.pop_front();
                        continue;
                    }
                }
                if( auto cast = llvm::dyn_cast<llvm::CastInst>(Q.front()) )
                {
                    // dynamic allocations are often made as uint8_t arrays and cast to the appropriate type
                    for( unsigned i = 0; i < cast->getNumOperands(); i++ )
                    {
                        if( covered.find( cast->getOperand(i) ) == covered.end() )
                        {
                            covered.insert(cast->getOperand(i));
                            Q.push_back(cast->getOperand(i));
                        }
                    }
                    // they can also cast pointer allocations to the type of the base pointer, so we have to put uses of the cast into the queue too
                    for( const auto& user : cast->users() )
                    {
                        if( covered.find(user) == covered.end() )
                        {
                            Q.push_back(user);
                            covered.insert(user);
                        }
                    }
                }
                else if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
                {
                    ldsnsts.insert(ld);
                    for( const auto& use : ld->users() )
                    {
                        if( covered.find(use) == covered.end() )
                        {
                            Q.push_back(use);
                            covered.insert(use);
                        }
                    }
                }
                else if( const auto& st = llvm::dyn_cast<llvm::StoreInst>(Q.front()) )
                {
                    ldsnsts.insert(st);
                    if( covered.find(st->getPointerOperand()) == covered.end() )
                    {
                        covered.insert(st->getPointerOperand());
                        Q.push_back(st->getPointerOperand());
                    }
                    // with stores, we evaluate the value operand as well
                    // for example in case an allocation is put into a double pointer, the value operand will lead back to the allocation, the pointer operand will lead to a static pointer allocation
                    if( covered.find(st->getValueOperand()) == covered.end() )
                    {
                        covered.insert(st->getValueOperand());
                        Q.push_back(st->getValueOperand());
                    }
                }
                else if( auto inst = llvm::dyn_cast<llvm::Instruction>(Q.front()) )
                {
                    for( const auto& use : inst->users() )
                    {
                        if( covered.find(use) == covered.end() )
                        {
                            Q.push_back(use);
                            covered.insert(use);
                        }
                    }
                }
                Q.pop_front();
            }
            for( const auto& ldorst : ldsnsts )
            {
                if( SignificantMemInst.find( std::static_pointer_cast<Graph::Inst>(DNIDMap.at(ldorst)) ) != SignificantMemInst.end() )
                {
                    return ALLOC_THRESHOLD;
                }
            }
            return 0;
        }
    }
    return 0;
}