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

bool BasePointer::isOffset( const llvm::Value* val ) const 
{
    deque<const llvm::Value*> Q;
    set<const llvm::Value*> covered;
    Q.push_front(node->getVal());
    covered.insert(node->getVal());
    while( !Q.empty() )
    {
        if( Q.front() == val )
        {
            // this is the value we are looking for, return true
            return true;
        }
        if( const auto& st = llvm::dyn_cast<llvm::StoreInst>(Q.front()) )
        {
            // there is a corner case where a pointer gets alloc'd on the stack and a malloc'd pointer gets stored to that stack pointer
            // thus, when the base pointer gets stored to that pointer, we have to track that pointer
            if( st->getValueOperand() == node->getVal() )
            {
                if( covered.find(st->getPointerOperand()) == covered.end() )
                {
                    Q.push_back(st->getPointerOperand());
                    covered.insert(st->getPointerOperand());
                }
            }
        }
        else if( const auto& cast = llvm::dyn_cast<llvm::CastInst>(Q.front()) )
        {
            // when stores put our base pointer into an alloc, it may first cast that alloc before storing our BP to it
            // thus, we need to add the operand of the cast to the queue
            for( const auto& op : cast->operands() )
            {
                if( covered.find(op) == covered.end() )
                {
                    Q.push_back(op);
                    covered.insert(op);
                }
            }
            for( const auto& user : cast->users() )
            {
                if( covered.find(user) == covered.end() )
                {
                    Q.push_back(user);
                    covered.insert(user);
                }
            }
        }
        else if( const auto& arg = llvm::dyn_cast<llvm::Argument>(Q.front()) )
        {
            // base pointers can be arguments sometimes, we just look through their users like they are an instructions
            for( const auto& user : arg->users() )
            {
                if( covered.find(user) == covered.end() )
                {
                    Q.push_back(user);
                    covered.insert(user);
                }
            }
        }
        // default case, if this is an instruction we search through it
        else if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(Q.front()) )
        {
            for( const auto& user : Q.front()->users() )
            {
                if( covered.find(user) == covered.end() )
                {
                    Q.push_back(user);
                    covered.insert(user);
                }
            }
        }
        else
        {
            //throw AtlasException("BP offset method cannot handle this instruction!");
        }
        Q.pop_front();
    }
    return false;
}

uint32_t Cyclebite::Grammar::isAllocatingFunction(const llvm::CallBase* call)
{
    if( call->getCalledFunction() )
    {
        if( (call->getCalledFunction()->getName() == "malloc") || (call->getCalledFunction()->getName() == "calloc") || (call->getCalledFunction()->getName() == "_Znam") || (call->getCalledFunction()->getName() == "_Znwm") )
        {
            // return its allocation size
            // the functions identified above have a single argument - their allocation in size
            // if it is determinable, we return that value
            if( call->arg_size() == 1 )
            {
                if( const auto& con = llvm::dyn_cast<llvm::Constant>(call->getArgOperand(0)) )
                {
                    return (uint32_t)*con->getUniqueInteger().getRawData();
                }
            }
            else if( call->arg_size() == 2 )
            {
                // calloc case, the first argument is the number of allocations and the second is the size of each allocation
                const auto conSize = llvm::dyn_cast<llvm::Constant>(call->getArgOperand(0));
                const auto conAllo = llvm::dyn_cast<llvm::Constant>(call->getArgOperand(1));
                if( conSize && conAllo )
                {
                    return (uint32_t)*conSize->getUniqueInteger().getRawData() * (uint32_t)*conAllo->getUniqueInteger().getRawData();
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

const llvm::Value* Cyclebite::Grammar::getPointerSource(const llvm::Value* ptr)
{
    // in this method, we walk back through the DFG until we find a value that either
    // 1. gets its value from an unknown place (like a dynamic input or function argument)
    // 2. has a determined value (like a static constant)
    deque<const llvm::Value*> Q;
    set<const llvm::Value*> covered;
    Q.push_front(ptr);
    covered.insert(ptr);
    while( !Q.empty() )
    {
        if( const auto& alloc = llvm::dyn_cast<llvm::AllocaInst>(Q.front()) )
        {
            // this is a source
            return alloc;
        }
        else if( const auto& call = llvm::dyn_cast<llvm::CallBase>(Q.front()) )
        {
            if( isAllocatingFunction(call) )
            {
                return call;
            }
            // otherwise there is no way for us to track a pointer through the operands of a function, so this is a dead end
        }
        else if( const auto& con = llvm::dyn_cast<llvm::Constant>(Q.front()) )
        {
            if( con->getType()->isPointerTy() )
            {
                return con;
            }
            // otherwise a constant is a dead end
        }
        else if( const auto& arg = llvm::dyn_cast<llvm::Argument>(Q.front()) )
        {
            // check the significant pointer list
            if( Graph::DNIDMap.find(arg) != DNIDMap.end() )
            {
                if( SignificantMemInst.find( Graph::DNIDMap.at(arg) ) != SignificantMemInst.end() )
                {
                    return arg;
                }
            }
        }
        else if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
        {
            Q.push_back(ld->getPointerOperand());
            covered.insert(ld->getPointerOperand());
        }
        else if( const auto& gep = llvm::dyn_cast<llvm::GetElementPtrInst>(Q.front()) )
        {
            Q.push_back(gep->getPointerOperand());
            covered.insert(gep->getPointerOperand());
        }
        else if( const auto& cast = llvm::dyn_cast<llvm::CastInst>(Q.front()) )
        {
            Q.push_back(cast->getOperand(0));
            covered.insert(cast->getOperand(0));
        }
        else if( const auto& con = llvm::dyn_cast<llvm::Constant>(Q.front()) )
        {
            // this may be a global pointer, return that
            if( con->getType()->isPointerTy() )
            {
                return con;
            }
            else if( con->getType()->isFunctionTy() )
            {
                // sometimes functions can return array types that are later indexed
                // e.g. Harris/API/nvision (-O2 BBID8, @_ZSt4cerr)
                // in this case we are interested in returning the function itself... because this is the source of the pointer
                return con;
            }
        }
        Q.pop_front();
    }
    spdlog::warn("Could not find source of pointer "+PrintVal(ptr, false));
    return ptr;
}