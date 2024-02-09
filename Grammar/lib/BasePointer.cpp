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

const llvm::Type* BasePointer::getContainedType() const
{
    // llvm no longer supports contained types in their class definition - types are inferred from the instructions
    // thus, to find the contained primitive type of this base pointer, we have to walk the DFG looking for geps
    // - when we find a load, it will extract a certain type from the pointer - this gives us our answer
    //   -- corner case: sometimes the load is a byte array that is casted to something else before it is used - we want the type from that case, not the loaded type
    deque<const llvm::Value*> Q;
    set<const llvm::Value*> covered;
    Q.push_front(node->getVal());
    covered.insert(node->getVal());
    while( !Q.empty() )
    {
        if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
        {
            // check the returned type of the load
            if( !llvm::isa<llvm::PointerType>(ld->getType()) )
            {
                if( const auto& ar = llvm::dyn_cast<llvm::ArrayType>(ld->getType()) )
                {
                    return ar->getArrayElementType();
                }
                else if( const auto& vt = llvm::dyn_cast<llvm::VectorType>(ld->getType()) )
                {
                    return vt->getElementType();
                }
                else if( const auto& st = llvm::dyn_cast<llvm::StructType>(ld->getType()) )
                {
                    throw CyclebiteException("Cannot yet support base pointers that house user-defined structures!");
                }
                else if( const auto& ft = llvm::dyn_cast<llvm::FunctionType>(ld->getType()) )
                {
                    throw CyclebiteException("Found a base pointer that holds a function type!");
                }
                else
                {
                    return ld->getType();
                }
            }
            else
            {
                for( const auto& use : ld->users() )
                {
                    if( !covered.contains(use) )
                    {
                        Q.push_back(use);
                        covered.insert(use);
                    }
                }
            }
        }
        else if( const auto& st = llvm::dyn_cast<llvm::StoreInst>(Q.front()) )
        {
            // base pointers can be put into local allocations
            // thus, if the base pointer is the value operand in this store, we need to follow the pointer now
            if( st->getValueOperand() == Q.front() )
            {
                if( !covered.contains(st->getPointerOperand()) )
                {
                    Q.push_back(st->getPointerOperand());
                    covered.insert(st->getPointerOperand());
                }
            }
        }
        else
        {
            for( const auto& use : Q.front()->users() )
            {
                if( !covered.contains(use) )
                {
                    Q.push_back(use);
                    covered.insert(use);
                }
            }
        }
        Q.pop_front();
    }
    return nullptr;
}

const string BasePointer::getContainedTypeString() const
{
    if( getContainedType() )
    {
        string typeStr;
        llvm::raw_string_ostream ty(typeStr);
        getContainedType()->print(ty);
        return typeStr;
    }
    return "UnknownType";
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
                                auto allocParam = alloc->getAllocationSizeInBits(alloc->getParent()->getParent()->getParent()->getDataLayout());
                                if( allocParam )
                                {
                                    auto allocSize = allocParam->getFixedValue()/8;
                                    if( allocSize >= ALLOC_THRESHOLD )
                                    {
                                        if( DNIDMap.contains(alloc) )
                                        {
                                            bpCandidates.insert(alloc);
                                        }
                                        else
                                        {
                                            PrintVal(alloc);
                                            spdlog::warn("Base-pointer-eligible alloc is not in the dynamic profile");
                                        }
                                        covered.insert(alloc);
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
                                else
                                {
                                    // when an alloca inst takes a dynamic parameter, we can't determine whether that pointer is useful (dynamically) 
                                    // thankfully we have significant memory instructions to count on
                                    if( Cyclebite::Graph::DNIDMap.contains(alloc) )
                                    {
                                        if( SignificantMemInst.contains( Cyclebite::Graph::DNIDMap.at(alloc) ) )
                                        {
                                            bpCandidates.insert(alloc);
                                            covered.insert(alloc);
                                        }
                                    }
                                }
                            }
                            else if( auto call = llvm::dyn_cast<llvm::CallBase>(Q.front()) )
                            {
                                if( isAllocatingFunction(call) >= ALLOC_THRESHOLD )
                                {
                                    if( DNIDMap.contains(call) )
                                    {
                                        // an allocating function is a base pointer
                                        bpCandidates.insert(call);
                                    }
                                    else
                                    {
                                        PrintVal(call);
                                        spdlog::warn("Base-pointer-eligible call is not in the dynamic profile");
                                    }
                                    covered.insert(call);
                                }
                            }
                            else if( auto arg = llvm::dyn_cast<llvm::Argument>(Q.front()) )
                            {
                                // we only care about arguments that are at least a pointer type (one or more indirection)
                                if( arg->getType()->isPointerTy() )
                                {
                                    if( DNIDMap.contains(arg) )
                                    {
                                        // an allocating function is a base pointer
                                        bpCandidates.insert(arg);
                                    }
                                    else
                                    {
                                        PrintVal(arg);
                                        spdlog::warn("Base-pointer-eligible arg is not in the dynamic profile");
                                    }
                                    covered.insert(arg);
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
                                        if( DNIDMap.contains(glob) )
                                        {
                                            // an allocating function is a base pointer
                                            bpCandidates.insert(glob);
                                        }
                                        else
                                        {
                                            PrintVal(glob);
                                            spdlog::warn("Base-pointer-eligible global is not in the dynamic profile");
                                        }
                                        covered.insert(glob);
                                    }
                                    else
                                    {
                                        for( const auto& user : glob->users() )
                                        {
                                            if( const auto& userInst = llvm::dyn_cast<llvm::Instruction>(user) )
                                            {
                                                Q.push_back(userInst);
                                                covered.insert(userInst);
                                            }
                                        }
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
    if( bpCandidates.empty() )
    {
        throw CyclebiteException("Could not find any base pointers in this task!");
    }
    // now turn all base pointers into objects
    set<shared_ptr<BasePointer>> bps;
    for( const auto& bp : bpCandidates )
    {
        bps.insert( make_shared<BasePointer>(DNIDMap.at(bp)) );
    }
    return bps;
}