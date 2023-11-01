//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Util/Exceptions.h"
#include "Util/Annotate.h"
#include "Util/Print.h"
#include "BasePointer.h"
#include "IO.h"
#include "Task.h"
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
            //throw CyclebiteException("BP offset method cannot handle this instruction!");
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
                throw CyclebiteException("Cannot determine allocator function size argument!");
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

set<shared_ptr<BasePointer>> Cyclebite::Grammar::getBasePointers(const shared_ptr<Task>& t)
{
    // in order to find base pointers, we introspect all load instructions, and walk backward through the pointer operand of a given load until we find a bedrock load (a load that uses a pointer with no offset - a magic number). The pointer of that load is a "base pointer"
    // base pointers are useful for modeling significant memory chunks. This input data represents an entity that can be used for communication
    // when base pointers are combined with the state variables (induction variables) that index them, Collections are formed (a space of memory in which the access pattern can be understood - a polyhedral space)
    set<const llvm::Value*> bpCandidates;
    set<const llvm::Value*> covered;
    for( const auto& c : t->getCycles() )
    {
        for( const auto& b : c->getBody() )
        {
            for( const auto& n : b->getInstructions() )
            {
                if( n->getOp() == Operation::load || n->getOp() == Operation::store )
                {
                    if( SignificantMemInst.find( n ) != SignificantMemInst.end() )
                    {
                        deque<const llvm::Value*> Q;
                        covered.insert(n->getVal());
                        Q.push_front(n->getVal());
                        while( !Q.empty() )
                        {
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
                            else if( auto ld = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
                            {
                                if( covered.find(ld->getPointerOperand()) == covered.end() )
                                {
                                    covered.insert(ld->getPointerOperand());
                                    Q.push_back(ld->getPointerOperand());
                                }
                            }
                            else if( auto st = llvm::dyn_cast<llvm::StoreInst>(Q.front()) )
                            {
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
                            else if( auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(Q.front()) )
                            {
                                if( covered.find(gep->getPointerOperand()) == covered.end() )
                                {
                                    covered.insert(gep->getPointerOperand());
                                    Q.push_back(gep->getPointerOperand());
                                }
                            }
                            else if( auto alloc = llvm::dyn_cast<llvm::AllocaInst>(Q.front()) )
                            {
                                // an originating alloc indicates a base pointer, if it is big enough
                                auto allocSize = alloc->getAllocationSizeInBits(alloc->getParent()->getParent()->getParent()->getDataLayout())->getFixedValue()/8;
                                if( allocSize >= ALLOC_THRESHOLD )
                                {
                                    bpCandidates.insert(alloc);
                                }
                                else
                                {
                                    spdlog::warn("Found allocation of size "+to_string(allocSize)+" bytes, which does not meet the minimum allocation size of "+to_string(ALLOC_THRESHOLD)+" for a base pointer.");
                                    // when we encounter allocs and they are too small, this likely means our base pointer is being stored to a pointer which contains the base pointer
                                    // thus, we need to add the users of this alloc to the queue
                                    for( const auto& user : alloc->users() )
                                    {
                                        if( covered.find(user) == covered.end() )
                                        {
                                            Q.push_back(user);
                                            covered.insert(user);
                                        }
                                    }
                                }
                            }
                            else if( auto call = llvm::dyn_cast<llvm::CallBase>(Q.front()) )
                            {
                                if( isAllocatingFunction(call) >= ALLOC_THRESHOLD )
                                {
                                    // an allocating function is a base pointer
                                    bpCandidates.insert(call);
                                }
                            }
                            else if( auto arg = llvm::dyn_cast<llvm::Argument>(Q.front()) )
                            {
                                // we only care about arguments that are at least a pointer type (one or more indirection)
                                if( arg->getType()->isPointerTy() )
                                {
                                    bpCandidates.insert(arg);
                                }
                            }
                            // when constant global structures are allocated, we need to identify their pointers
                            // ex: StencilChain/Naive (filter weight array)
                            else if( const auto& glob = llvm::dyn_cast<llvm::Constant>(Q.front()) )
                            {
                                if( glob->getType()->isPointerTy() )
                                {
                                    // must meet minimum pointer size
                                    bool canBeNull = false;
                                    bool canBeFreed = false;
                                    if( glob->getPointerDereferenceableBytes(n->getInst()->getParent()->getParent()->getParent()->getDataLayout(), canBeNull, canBeFreed) > ALLOC_THRESHOLD )
                                    {
                                        bpCandidates.insert(glob);
                                    }
                                }
                            }
                            Q.pop_front();
                        }
                    }
                }
            }
        }
    }
    // now turn all base pointers into objects
    set<shared_ptr<BasePointer>> bps;
    for( const auto& bp : bpCandidates )
    {
        // for each base pointer, we are interested in finding all load and store instructions that touch its memory
        // this should include the pointer itself
        set<const llvm::LoadInst*> lds;
        set<const llvm::StoreInst*> sts;
        set<const llvm::GetElementPtrInst*> geps;
        set<const llvm::Value*> covered;
        // this remembers the base pointer value through all of its transformations and changes
        // for example, when the base pointer is casted, this value will take on the cast
        const llvm::Instruction* basePointerInst = nullptr;
        deque<const llvm::Value*> Q;
        Q.push_front(bp);
        covered.insert(bp);
        while( !Q.empty() )
        {
            if( DNIDMap.find(Q.front()) == DNIDMap.end() )
            {
                // we don't care about dead instructions
                covered.insert(Q.front());
                Q.pop_front();
                continue;
            }
            if( auto arg = llvm::dyn_cast<llvm::Argument>(Q.front()) )
            {
                // arguments can only be used elsewhere so we search through its uses
                for( const auto& user : arg->users() )
                {
                    if( covered.find(user) == covered.end() )
                    {
                        covered.insert(user);
                        Q.push_back(user);
                    }
                }
            }
            else if( const auto& glob = llvm::dyn_cast<llvm::Constant>(Q.front()) )
            {
                // a constant pointer points to a static array that holds filter weights
                // this counts as a collection
                for( const auto& user : glob->users() )
                {
                    if( covered.find(user) == covered.end() )
                    {
                        Q.push_back(user);
                        covered.insert(user);
                    }
                }
            }
            else if( auto cast = llvm::dyn_cast<llvm::CastInst>(Q.front()) )
            {
                // a cast will turn the base pointer into its correct type (this happens when malloc allocates it)
                // thus we move the base pointer inst to this cast
                basePointerInst = cast;
                // sometimes a cast can point us to an allocated pointer that points to the base pointer
                // thus we need to add the operands of the cast as well to the search
                for( const auto& op : cast->operands() )
                {
                    if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(op) )
                    {
                        if( covered.find(inst) == covered.end() )
                        {
                            covered.insert(inst);
                            Q.push_back(inst);
                        }
                    }
                }
                for( const auto& user : cast->users() )
                {
                    if( covered.find(user) == covered.end() )
                    {
                        covered.insert(user);
                        Q.push_back(user);
                    }
                }
            }
            else if( auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(Q.front()) )
            {
                // we have found a target, and we assume that walking the DFG has given us an order to the GEPs
                // thus we push this gep in the order we found it
                if( t->find(DNIDMap[gep]) )
                {
                    geps.insert(gep);
                }
                for( const auto& user : gep->users() )
                {
                    if( covered.find(user) == covered.end() )
                    {
                        covered.insert(user);
                        Q.push_back(user);
                    }
                }
            }
            else if( auto ld = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
            {
                lds.insert(ld);
                for( const auto& user : ld->users() )
                {
                    if( covered.find(user) == covered.end() )
                    {
                        covered.insert(user);
                        Q.push_back(user);
                    }
                }
            }
            else if( auto st = llvm::dyn_cast<llvm::StoreInst>(Q.front()) )
            {
                // if the base pointer is being stored somewhere, we care about that
                // but the store instruction set is supposed to contain ops that store to the BP memory region
                // thus, if the BP itself is being stored, we skip it
                if( (basePointerInst != st->getValueOperand()) && (bp != st->getValueOperand()) )
                {
                    sts.insert(st);
                }
                if( const auto& ptr = llvm::dyn_cast<llvm::Instruction>(st->getPointerOperand()) )
                {
                    if( covered.find(ptr) == covered.end() )
                    {
                        covered.insert(ptr);
                        Q.push_back(ptr);
                    }
                }
            }
            else if( auto alloc = llvm::dyn_cast<llvm::AllocaInst>(Q.front()) )
            {
                // an alloc can be a pointer that points to the base pointer
                // thus we need to push it to the queue to investigate its uses
                for( const auto& user : alloc->users() )
                {
                    if( covered.find(user) == covered.end() )
                    {
                        Q.push_back(user);
                        covered.insert(user);
                    }
                }
            }
            else if( auto call = llvm::dyn_cast<llvm::CallBase>(Q.front()) )
            {
                for( const auto& user : call->users() )
                {
                    if( covered.find(user) == covered.end() )
                    {
                        Q.push_back(user);
                        covered.insert(user);
                    }
                }
            }
            Q.pop_front();
        }
#ifdef DEBUG
        if( geps.empty() )
        {
            PrintVal(bp);
            throw CyclebiteException("Could not map any geps to a base pointer candidate!");
        }
#endif
        // now we group the loads and stores together with their respective geps in the order they appear
        // first, loads
        set<pair<const llvm::GetElementPtrInst*, const llvm::LoadInst*>> loadPairs;
        deque<const llvm::Instruction*> orderQ;
        set<const llvm::Instruction*> orderCover;
        for( const auto& ld : lds )
        {
            // for each load we will 
            // 1. gather its gep and group them
            // 2. find out if this is the last gep on the base pointer before it is used for function (that gep will start the evaluation for gep ordering)
            Q.clear();
            covered.clear();
            Q.push_front(ld->getPointerOperand());
            covered.insert(ld->getPointerOperand());
            bool gotPair = false;
            while( !Q.empty() )
            {
                if( DNIDMap.find(Q.front()) == DNIDMap.end() )
                {
                    covered.insert(Q.front());
                    Q.pop_front();
                    continue;
                }
                if( const auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(Q.front()) )
                {
                    loadPairs.insert(pair<const llvm::GetElementPtrInst*, const llvm::LoadInst*>(gep, ld));
                    gotPair = true;
                    // sometimes the geps compound, ie they gep a multi-indirected pointer
                    // thus we must search through the gep pointer
                    if( covered.find( gep->getPointerOperand()) == covered.end() )
                    {
                        Q.push_back(gep->getPointerOperand());
                        covered.insert(gep->getPointerOperand());
                    }
                }
                else if( const auto ld = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
                {
                    // there may be many loads to get all the way down to the base pointer
                    if( covered.find(ld->getPointerOperand()) == covered.end() )
                    {
                        Q.push_back(ld->getPointerOperand());
                        covered.insert(ld->getPointerOperand());
                    }
                }
                else if( const auto& cast = llvm::dyn_cast<llvm::CastInst>(Q.front()) )
                {
                    // casts can occur between loads when vectors are used
                    for( const auto& op : cast->operands() )
                    {
                        if( const auto& opInst = llvm::dyn_cast<llvm::Instruction>(op) )
                        {
                            if( covered.find(opInst) == covered.end() )
                            {
                                Q.push_back(opInst);
                                covered.insert(opInst);
                            }
                        }
                    }
                }
                else if( const auto inst = llvm::dyn_cast<llvm::Instruction>(Q.front()) )
                {
                    if( static_pointer_cast<Inst>(DNIDMap.at(inst))->isMemory() )
                    {
                        // we are out of the memory group, which definitely means we have completed our search
                        Q.clear();
                        break;
                    }
                }
                Q.pop_front();
            }
#ifdef DEBUG
            if( !gotPair )
            {
                // this can occur when a base pointer is stored within an alloc
                // in that case, the load never uses a gep... it just loads the pointer
                // so just ignore it for now
                //PrintVal(ld);
                //throw CyclebiteException("Could not map a base pointer load to a gep!");
                continue;
            }
#endif
            // second, if this is a load that leaves the memory group, mark it
            bool found = false;
            for( const auto& succ : DNIDMap.at(ld)->getSuccessors() )
            {
                if( static_pointer_cast<Inst>(succ->getSnk())->isMemory() )
                {
                    found = true;
                    break;
                }
            }
            if( !found )
            {
                orderQ.push_front(ld);
                orderCover.insert(ld);
            }
        }
        // second, gep ordering for loads
        vector<const llvm::GetElementPtrInst*> ldOrdering;
        while( !orderQ.empty() )
        {
            if( DNIDMap.find(orderQ.front()) == DNIDMap.end() )
            {
                orderCover.insert(orderQ.front());
                orderQ.pop_front();
                continue;
            }
            if( const auto& gep = llvm::dyn_cast<const llvm::GetElementPtrInst>(orderQ.front()) )
            {
                ldOrdering.push_back(gep);
                if( const auto& ptr = llvm::dyn_cast<const llvm::Instruction>(gep->getPointerOperand()) )
                {
                    if( orderCover.find(ptr) == orderCover.end() )
                    {
                        orderQ.push_back(ptr);
                        orderCover.insert(ptr);
                    }
                }
            }
            else if( const auto& ld = llvm::dyn_cast<const llvm::LoadInst>(orderQ.front()) )
            {
                if( const auto& ptr = llvm::dyn_cast<const llvm::Instruction>(ld->getPointerOperand()) )
                {
                    if( orderCover.find(ptr) == orderCover.end() )
                    {
                        orderQ.push_back(ptr);
                        orderCover.insert(ptr);
                    }
                }
            }
            else if( const auto& cast = llvm::dyn_cast<llvm::CastInst>(orderQ.front()) )
            {
                // casts can occur between loads when vectors are used
                for( const auto& op : cast->operands() )
                {
                    if( const auto& opInst = llvm::dyn_cast<llvm::Instruction>(op) )
                    {
                        if( orderCover.find(opInst) == orderCover.end() )
                        {
                            orderQ.push_back(opInst);
                            orderCover.insert(opInst);
                        }
                    }
                }
            }
            orderQ.pop_front();
        }
        vector<pair<const llvm::GetElementPtrInst*, const llvm::LoadInst*>> loads;
        for( const auto& gep : ldOrdering )
        {
            pair<const llvm::GetElementPtrInst*, const llvm::LoadInst*> targetPair(nullptr, nullptr);
            for( const auto& p : loadPairs )
            {
                if( p.first == gep )
                {
                    // geps can map to multiple loads when the optimizer is turned on
                    targetPair = p;
                    break;
                }
            }
            if( targetPair.first == nullptr || targetPair.second == nullptr )
            {
                PrintVal(gep);
                throw CyclebiteException("Found a gep that doesn't map to a load instruction!");
            }
            loads.push_back(targetPair);
        }
        // third, stores
        set<pair<const llvm::GetElementPtrInst*, const llvm::StoreInst*>> storePairs;
        // this starts with finding the loads that feed a functional group
        orderQ.clear();
        orderCover.clear();
        for( const auto& st : sts )
        {
            // for each store we will 
            // 1. gather its gep and group them
            // 2. find out if this is the last gep on the base pointer before it is used for function (that gep will start the evaluation for gep ordering)
            Q.clear();
            covered.clear();
            Q.push_front(st->getPointerOperand());
            covered.insert(st->getPointerOperand());
            bool gotPair = false;
            while( !Q.empty() )
            {
                if( DNIDMap.find(Q.front()) == DNIDMap.end() )
                {
                    covered.insert(Q.front());
                    Q.pop_front();
                    continue;
                }
                if( const auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(Q.front()) )
                {
                    storePairs.insert(pair<const llvm::GetElementPtrInst*, const llvm::StoreInst*>(gep, st));
                    gotPair = true;
                    // sometimes the geps compound, ie they gep a multi-indirected pointer
                    // thus we must search through the gep pointer
                    if( covered.find( gep->getPointerOperand()) == covered.end() )
                    {
                        Q.push_back(gep->getPointerOperand());
                        covered.insert(gep->getPointerOperand());
                    }
                }
                else if( const auto ld = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
                {
                    // there may be many loads to get all the way down to the base pointer
                    if( covered.find(ld->getPointerOperand()) == covered.end() )
                    {
                        Q.push_back(ld->getPointerOperand());
                        covered.insert(ld->getPointerOperand());
                    }
                }
                else if( const auto& cast = llvm::dyn_cast<llvm::CastInst>(Q.front()) )
                {
                    // casts can sit between a gep and the store
                    for( const auto& op : cast->operands() )
                    {
                        if( const auto& opInst = llvm::dyn_cast<llvm::Instruction>(op) )
                        {
                            if( covered.find(opInst) == covered.end() )
                            {
                                Q.push_back(opInst);
                                covered.insert(opInst);
                            }
                        }
                    }
                }
                else if( const auto inst = llvm::dyn_cast<llvm::Instruction>(Q.front()) )
                {
                    if( !static_pointer_cast<Inst>(DNIDMap.at(inst))->isMemory() )
                    {
                        // we are out of the memory group, which definitely means we have completed our search
                        Q.clear();
                        break;
                    }
                }
                Q.pop_front();
            }
            if( !gotPair )
            {
                // it is possible to encounter a situation where a store is not accompanied by a gep
                // (see GEMM/Naive/BB20)
#ifdef DEBUG
                PrintVal(st);
                spdlog::warn("Store pointer was not gep'd");
#endif
                storePairs.insert(pair<const llvm::GetElementPtrInst*, const llvm::StoreInst*>(nullptr, st));
            }
            // second, if this is a store that is fed by the function group, mark it
            bool func = true;
            /*for( const auto& succ : DNIDMap.at(st)->getPredecessors() )
            {
                // in random init kernels, the value operand is just a function and the pointer is the base pointer
                // the problem is, the graph coloring algorithm doesn't color the function call as the functional group
                // thus, this code doesn't work (as of 2023-06-20), so we don't do this check 
                if( !static_pointer_cast<Inst>(succ->getSrc())->isFunction() )
                {
                    func = false;
                    break;
                }
            }*/
            if( func )
            {
                orderQ.push_front(st);
                orderCover.insert(st);
            }
        }
        // fourth, gep ordering for stores
        vector<const llvm::GetElementPtrInst*> stOrdering;
        while( !orderQ.empty() )
        {
            if( DNIDMap.find(orderQ.front()) == DNIDMap.end() )
            {
                orderCover.insert(orderQ.front());
                orderQ.pop_front();
                continue;
            }
            if( const auto& gep = llvm::dyn_cast<const llvm::GetElementPtrInst>(orderQ.front()) )
            {
                stOrdering.push_back(gep);
                if( const auto& ptr = llvm::dyn_cast<const llvm::Instruction>(gep->getPointerOperand()) )
                {
                    if( orderCover.find(ptr) == orderCover.end() )
                    {
                        orderQ.push_back(ptr);
                        orderCover.insert(ptr);
                    }
                }
            }
            else if( const auto& ld = llvm::dyn_cast<const llvm::LoadInst>(orderQ.front()) )
            {
                if( const auto& ptr = llvm::dyn_cast<const llvm::Instruction>(ld->getPointerOperand()) )
                {
                    if( orderCover.find(ptr) == orderCover.end() )
                    {
                        orderQ.push_back(ptr);
                        orderCover.insert(ptr);
                    }
                }
            }
            else if( const auto& st = llvm::dyn_cast<const llvm::StoreInst>(orderQ.front()) )
            {
                if( const auto& ptr = llvm::dyn_cast<const llvm::Instruction>(st->getPointerOperand()) )
                {
                    if( orderCover.find(ptr) == orderCover.end() )
                    {
                        orderQ.push_back(ptr);
                        orderCover.insert(ptr);
                    }
                }
            }
            else if( const auto& cast = llvm::dyn_cast<llvm::CastInst>(orderQ.front()) )
            {
                // casts can occur between loads when vectors are used
                for( const auto& op : cast->operands() )
                {
                    if( const auto& opInst = llvm::dyn_cast<llvm::Instruction>(op) )
                    {
                        if( orderCover.find(opInst) == orderCover.end() )
                        {
                            orderQ.push_back(opInst);
                            orderCover.insert(opInst);
                        }
                    }
                }
            }
            orderQ.pop_front();
        }
        vector<pair<const llvm::GetElementPtrInst*, const llvm::StoreInst*>> stores;
        // stOrdering is in reverse order, so we reverse the iterator
        for( auto rgep = stOrdering.rbegin(); rgep < stOrdering.rend(); rgep++ )
        {
            auto gep = *rgep;
            pair<const llvm::GetElementPtrInst*, const llvm::StoreInst*> targetPair(nullptr, nullptr);
            for( const auto& p : storePairs )
            {
                if( p.first == gep )
                {
                    // geps can map to multiple stores when the optimizer is turned on
                    targetPair = p;
                    break;
                }
            }
            if( targetPair.first == nullptr || targetPair.second == nullptr )
            {
                PrintVal(bp);
                PrintVal(gep);
                throw CyclebiteException("Found a gep that doesn't map to a store instruction!");
            }
            stores.push_back(targetPair);
        }
        bps.insert( make_shared<BasePointer>(DNIDMap.at(bp), loads, stores) );
    }
    if( bps.empty() )
    {
        throw CyclebiteException("Could not find any base pointers in this task!");
    }
    return bps;
}