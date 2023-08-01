#include "Process.h"
#include "DataGraph.h"
#include "ControlBlock.h"
#include "ConstantSymbol.h"
#include "Graph/inc/IO.h"
#include "inc/IO.h"
#include "Reduction.h"
#include "ConstantFunction.h"
#include "Util/Annotate.h"
#include "Util/Print.h"
#include "Task.h"
#include "Dijkstra.h"
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <deque>

using namespace Cyclebite::Grammar;
using namespace Cyclebite::Graph;
using namespace std;

set<shared_ptr<InductionVariable>> Cyclebite::Grammar::getInductionVariables(const shared_ptr<Task>& t)
{
    // in order to understand the function and dimensionality of an algorithm we need two things
    // 1. an expression (nodes in the function category) to map to a Halide function
    // - we already have 1 from previous analysis, thus we are interested in mapping the conditional branches to their "sources"
    // 2. the dimensionality of the algorithm (the conditional branches) that map to vars in the function
    // - a "source" of the conditional branch is the entity that drives the state of that branch (that is, the "induction variable" that is compared to a condition to produce a decision)
    // - there are three common cases
    //     1. a conditional branch fed by a cmp fed by a ld (the variable lives in the heap)
    //     2. a conditional branch fed by a cmp fed by an add/sub/mul/div with a circular dataflow with a phi (the variable lives in a value)
    // 3. how each dimension "interacts" (what order should the vars be in?)
    // - this is done by evaluating how the memory space uses state to decide where to read/write
    set<shared_ptr<InductionVariable>> IVs;
    // the algorithm is as follows
    // 1. find the sources of the conditional branches. Each source maps 1:1 with a Var
    // 2. for each user of each source
    //      evaluate its users (look for GEPs that feed loads that feed function constituents)... the GEPs that appear first will be using "higher-dimensional" Vars 

    // induction variables are exclusively for the facilitation of cyclical behavior.
    // thus, we will start from all the cycle-inducing instructions, walk backwards through the graph, and find the IVs (likely through PHIs and ld/st with the same pointer)
    for( const auto& cycle : t->getCycles() )
    {
        auto d = static_pointer_cast<Inst>(DNIDMap.at(cycle->getIteratorInst()));
        if( (d->isTerminator()) && (d->parent->getSuccessors().size() > 1) )
        {
            // we have a multi-destination control instruction, walk its predecessors to find a memory or binary operation that indicates an induction variable
            set<shared_ptr<DataValue>, p_GNCompare> vars;
            set<const llvm::Instruction*> covered;
            deque<const llvm::Instruction*> Q;
            Q.push_front(llvm::cast<llvm::Instruction>(d->getVal()));
            covered.insert(llvm::cast<llvm::Instruction>(d->getVal()));
            while( !Q.empty() )
            {
                for( auto& use : Q.front()->operands() )
                {
                    if( const auto& useInst = llvm::dyn_cast<llvm::Instruction>(use.get()) )
                    {
                        if( !cycle->find(DNIDMap.at(useInst)) )
                        {
                            continue;
                        }
                    }
                    if( auto cmp = llvm::dyn_cast<llvm::CmpInst>(use.get()) )
                    {
                        if( covered.find(cmp) == covered.end() )
                        {
                            Q.push_back(cmp);
                            covered.insert(cmp);
                        }
                    }
                    else if( auto bin = llvm::dyn_cast<llvm::BinaryOperator>(use.get()) )
                    {
                        if( covered.find(bin) == covered.end() )
                        {
                            Q.push_back(bin);
                            covered.insert(bin);
                        }
                    }
                    else if( auto phi = llvm::dyn_cast<llvm::PHINode>(use.get()) )
                    {
                        // case found in optimized programs when an induction variable lives in a value (not the heap) and has a DFG cycle between an add and a phi node 
                        if( auto bin = llvm::dyn_cast<llvm::BinaryOperator>(Q.front()) )
                        {
                            // we have found a cycle between a binary op and a phi, likely indicating an induction variable, thus add it to the set of dimensions
                            covered.insert(bin);
                            covered.insert(phi);
                            vars.insert( Cyclebite::Graph::DNIDMap.at((llvm::Instruction*)phi) );
                        }
                    }
                    else if( auto ld = llvm::dyn_cast<llvm::LoadInst>(use.get()) )
                    {
                        // case found in unoptimized programs when the induction variable lives on the heap (not in a value) and is communicated with through ld/st
                        // the pointer argument to this load is likely the induction variable pointer, so add that to the vars set
                        covered.insert(ld);
                        if( const auto& p_inst = llvm::dyn_cast<llvm::Instruction>(ld->getPointerOperand()) )
                        {
                            if( BBCBMap.find(p_inst->getParent()) != BBCBMap.end() )
                            {
                                // now we know it is allive
                                vars.insert( Cyclebite::Graph::DNIDMap.at((llvm::Instruction*)ld->getPointerOperand()) );
                            }
                        }
                    }
                }
                Q.pop_front();
            }
            if( vars.size() != 1 )
            {
                for( const auto& var : vars )
                {
                    PrintVal(var->getVal());
                }
                throw AtlasException("Found more than one IV candidate for this cycle!");
            }
            auto newIV = make_shared<InductionVariable>(*vars.begin(), cycle);
            IVs.insert(newIV);
        }
    }
    return IVs;
}

set<shared_ptr<ReductionVariable>> Cyclebite::Grammar::getReductionVariables(const shared_ptr<Task>& t, const set<shared_ptr<InductionVariable>>& vars)
{
    set<shared_ptr<ReductionVariable>> rvs;
    // these stores have a value operand that comes from the functional group
    set<const llvm::StoreInst*> sts;
    for( const auto& c : t->getCycles() )
    {
        for( const auto& b : c->getBody() )
        {
            for( const auto& i : b->instructions )
            {
                if( const auto st = llvm::dyn_cast<llvm::StoreInst>(i->getVal()) )
                {
                    if( const auto& v = llvm::dyn_cast<llvm::Instruction>(st->getValueOperand()) )
                    {
                        if( static_pointer_cast<Inst>(DNIDMap.at(v))->isFunction() )
                        {
                            sts.insert(st);
                        }
                    }
                }
            }
        }
    }
    for( const auto& s : sts )
    {
        // reduction variables search
        // walk the value operands of those instructions to start the hunt for reduction variables
        // reduction variables (rv) look very much like induction variables (iv) in that they commonly come in two flavors
        // 1. they are loaded from and stored to through indirect pointers (like directly storing to the array index instead of a local value, common in unoptimized cases)
        // 2. they loop with a phi (common to optimized cases)
        // the difference between rv and iv is that rv lies entirely within a functional group
        // thus we crawl the functional group and put each value through checks similar to the getInductionVariables() method
        // we have a multi-destination control instruction, walk its predecessors to find a memory or binary operation that indicates an induction variable
        set<shared_ptr<DataValue>> reductionCandidates;
        deque<const llvm::Instruction*> Q;
        set<const llvm::Instruction*> seen;
        shared_ptr<DataValue> reductionOp = nullptr;
        Q.push_front(llvm::cast<llvm::Instruction>(s->getValueOperand()));
        seen.insert(llvm::cast<llvm::Instruction>(s->getValueOperand()));
        while( !Q.empty() )
        {
            for( auto& use : Q.front()->operands() )
            {
                // binary instructions should be the only type of instruction to lead us to a phi
                if( auto bin = llvm::dyn_cast<llvm::BinaryOperator>(use.get()) )
                {
                    if( seen.find(bin) == seen.end() )
                    {
                        // we only mark the binary op we see that is closest to the store
                        if( !reductionOp )
                        {
                            reductionOp = DNIDMap.at(bin);
                        }
                        Q.push_back(bin);
                        seen.insert(bin);
                    }
                }
                else if( auto phi = llvm::dyn_cast<llvm::PHINode>(use.get()) )
                {
                    // case found in optimized programs when an induction variable lives in a value (not the heap) and has a DFG cycle between an add and a phi node 
                    if( auto bin = llvm::dyn_cast<llvm::BinaryOperator>(Q.front()) )
                    {
                        // we have found a cycle between a binary op and a phi, likely indicating an induction variable, thus add it to the set of dimensions
                        seen.insert(bin);
                        seen.insert(phi);
                        reductionOp = DNIDMap.at(bin);
                        reductionCandidates.insert(DNIDMap.at(phi));
                    }
                }
                else if( auto ld = llvm::dyn_cast<llvm::LoadInst>(use.get()) )
                {
                    // a load's pointer may point us back to a store we have seen
                    // this will lead us back to a reduction variable pointer (in the case of unoptimized code)

                }
                else if( auto st = llvm::dyn_cast<llvm::StoreInst>(use.get()) )
                {
                    // case found in unoptimized programs when the induction variable lives on the heap (not in a value) and is communicated with through ld/st
                    // the pointer argument to this store is likely the induction variable pointer, so add that to the reductions set
                    seen.insert(st);
                    if( const auto ptr = llvm::dyn_cast<llvm::Instruction>(st->getPointerOperand()) )
                    {
                        reductionCandidates.insert(DNIDMap.at(ptr));
                    } 
                }
            }
            Q.pop_front();
        }
        // now find the induction variable associated with each reduction variable candidate
        for( const auto& can : reductionCandidates )
        {
            // it is possible to find the same reduction candidate twice when operations are partially unrolled
            // to handle this case we do a check on the rvs that already exist and skip the ones that may be a repeat
            bool skip = false;
            for( const auto& rv : rvs )
            {
                if( reductionOp == rv->getNode() )
                {
                    skip = true;
                    break;
                }
            }
            if( skip ) { continue; }
            shared_ptr<InductionVariable> iv = nullptr;
            // the store value operand is within the cycle that contains the RV
            // to find the induction variable associated with that candidate, the 
            if( const auto& v = llvm::dyn_cast<llvm::Instruction>(s->getValueOperand()) )
            {
                auto b = static_pointer_cast<Inst>(DNIDMap.at(v));
                for( const auto& var : vars )
                {
                    if( var->getCycle()->find(b->parent) )
                    {
                        iv = var;
                    }
                }
            }
            else
            {
                PrintVal(s);
                throw AtlasException("Store instruction of an induction variable is not an instruction!");
            }
            if( iv == nullptr )
            {
                PrintVal(can->getVal());
                throw AtlasException("Cannot map this reduction variable to an induction variable!");
            }
            rvs.insert( make_shared<ReductionVariable>(iv, reductionOp) );
        }
    }
    return rvs;
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
            for( const auto& n : b->instructions )
            {
                if( n->op == Operation::load || n->op == Operation::store )
                {
                    if( SignificantMemInst.find( n ) != SignificantMemInst.end() )
                    {
                        deque<const llvm::Value*> Q;
                        covered.insert(n->getVal());
                        Q.push_front(n->getVal());
                        while( !Q.empty() )
                        {
                            if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(Q.front()) )
                            {
                                if( GetBlockID(inst->getParent()) == IDState::Uninitialized )
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
                                auto allocSize = alloc->getAllocationSizeInBits(alloc->getParent()->getParent()->getParent()->getDataLayout()).getValue()/8;
                                if( allocSize >= ALLOC_THRESHOLD )
                                {
                                    bpCandidates.insert(alloc);
                                }
                                else
                                {
                                    spdlog::warn("Found allocation of size "+to_string(allocSize)+" bytes, which does not meet the minimum allocation size for a base pointer.");
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
                                if( isAllocatingFunction(call) )
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
            throw AtlasException("Could not map any geps to a base pointer candidate!");
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
                //throw AtlasException("Could not map a base pointer load to a gep!");
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
                throw AtlasException("Found a gep that doesn't map to a load instruction!");
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
#ifdef DEBUG
            if( !gotPair )
            {
                PrintVal(st);
                throw AtlasException("Could not map a base pointer store to a gep!");
            }
#endif
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
                throw AtlasException("Found a gep that doesn't map to a store instruction!");
            }
            stores.push_back(targetPair);
        }
        bps.insert( make_shared<BasePointer>(DNIDMap.at(bp), loads, stores) );
    }
    if( bps.empty() )
    {
        throw AtlasException("Could not find any base pointers in this task!");
    }
    return bps;
}

bool hasConstantOffset(const llvm::GetElementPtrInst* gep )
{
    bool allConstant = true;
    for( const auto& idx : gep->indices() )
    {
        if( !llvm::isa<llvm::Constant>(idx) )
        {
            allConstant = false;
            break;
        }
    }
    return allConstant;
}

vector<shared_ptr<InductionVariable>> getOrdering( const llvm::GetElementPtrInst* gep, const set<shared_ptr<InductionVariable>>& IVs )
{
    vector<shared_ptr<InductionVariable>> order;
    for( auto idx = gep->idx_begin(); idx != gep->idx_end(); idx++ )
    {
        bool found = false;
        for( const auto& iv : IVs )
        {
            if( (iv->getNode()->getVal() == *idx) || (iv->isOffset(*idx)) )
            {
                found = true;
                order.push_back(iv);
                break;
            }
            else if( const auto& con = llvm::dyn_cast<llvm::Constant>(*idx) )
            {
                // skip
                found = true;
                break;
            }
            else if( const auto& glob = llvm::dyn_cast<llvm::GlobalValue>(*idx) )
            {
                // not sure what to do here
                throw AtlasException("Found a global in a gep!");
            }
        }
        if( !found )
        {
            PrintVal(gep);
            throw AtlasException("Cannot map a gep index to an induction variable!");
        }
    }
    return order;
}

set<shared_ptr<Collection>> Cyclebite::Grammar::getCollections(const shared_ptr<Task>& t, const set<shared_ptr<InductionVariable>>& IVs, const set<shared_ptr<BasePointer>>& BPs)
{
    // collections map IVs to BPs
    // an IV and a BP form a collection of an IV is used at least once to offset a BP
    // often, multiple IVs map to one BP
    // - this case forms one collection, and the IVs are ordered in the order they offset the BP i.e. bp[i][j] is offset by i first, thus i is the "first" IV in the collection, j is second
    set<shared_ptr<Collection>> colls;
    // map IVs to the geps that use them
    // these will be used to "group" IVs that work together to index a base pointer
    map<shared_ptr<InductionVariable>, set<const llvm::GetElementPtrInst*>> ivToGep;
    for( const auto& iv : IVs )
    {
        set<const llvm::Value*> covered;
        deque<const llvm::Value*> Q;
        // there may be several instructions between a GEPs offset and the IV, and the best way to address this is to walk forward starting at the IVs, and then doing a check every time a GEP is hit
        set<const llvm::GetElementPtrInst*> geps;
        Q.push_front(iv->getNode()->getVal());
        covered.insert(iv->getNode()->getVal());
        while( !Q.empty() )
        {
            for( const auto& user : Q.front()->users() )
            {
                if( auto bin = llvm::dyn_cast<llvm::BinaryOperator>(user) )
                {
                    if( covered.find(bin) == covered.end() )
                    {
                        covered.insert(bin);
                        Q.push_back(bin);
                    }
                }
                else if( auto cast = llvm::dyn_cast<llvm::CastInst>(user) )
                {
                    if( covered.find( cast ) == covered.end() )
                    {
                        covered.insert(cast);
                        Q.push_back(cast);
                    }
                }
                else if( auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(user) )
                {
                    // try to find this gep in the ordered list, 
                    if( covered.find(gep) == covered.end() )
                    {
                        geps.insert(gep);
                        covered.insert(gep);
                        Q.push_back(gep);
                    }
                }
                else if( auto ld = llvm::dyn_cast<llvm::LoadInst>(user) )
                {
                    if( covered.find(ld) == covered.end() )
                    {
                        covered.insert(ld);
                        Q.push_back(ld);
                    }
                }
            }
            Q.pop_front();
        }
        ivToGep[iv] = geps;
    }
    // now use the mapping IV2gep to group IVs together
    // IVs that overlap in their geps indicate which geps work together 
    map<shared_ptr<InductionVariable>, set<shared_ptr<InductionVariable>>> ivToGroup;
    for( const auto& iv : ivToGep )
    {
        for( const auto& iv2 : ivToGep )
        {
            if( iv.first == iv2.first )
            {
                continue;
            }
            for( const auto& gep : iv.second )
            {
                if( iv2.second.find(gep) != iv2.second.end() )
                {
                    // an overlap has been found, these two IVs work together to offset the same pointer. Thus they should be used together in a collection
                    ivToGroup[iv.first].insert(iv2.first);
                    ivToGroup[iv2.first].insert(iv.first);
                    // now make the sets coherent
                    ivToGroup[iv.first].insert( ivToGroup[iv2.first].begin(), ivToGroup[iv2.first].end() );
                    ivToGroup[iv2.first].insert( ivToGroup[iv.first].begin(), ivToGroup[iv.first].end() );
                    break;
                }
            }
        }
    }
    // now that we have groups of IVs that work together, map them to base pointers
    // we do this by finding IVs and BPs that touch the same GEPs
    for( const auto& bp : BPs )
    {
        // for each gep that touches the BP, find the var that touches that GEP too
        set<shared_ptr<InductionVariable>> unorderedVars;
        // each base pointer has geps that may or may not be in this task
        // we are only interested in the task geps, so make a set with just them and only pay attention to them
        set<const llvm::GetElementPtrInst*> taskGeps;
        for( const auto& gep : bp->getgps() )
        {
            if( t->find(DNIDMap.at(gep)) )
            {
                taskGeps.insert(gep);
            }
        }
        if( taskGeps.empty() )
        {
            throw AtlasException("Base pointer has no geps in the current task!");
        }
        for( const auto& gep : taskGeps )
        {
            // there are two cases here
            // first, we are adding an IV to a base pointer, in that case you need to match an IV to it
            // second, we are adding a constant to a base pointer (commonly found in LLVM's front-end vector-optimized form), then we skip it (FOR NOW 2023-06-20) because that gives us no useful information
            if( !hasConstantOffset(gep) )
            {
                // it is possible for multiple IVs to map to a single gep (see naive GEMM on -O3)
                // thus we look for all IVs that map to this gep, not just a single one
                for( const auto& iv : ivToGep )
                {
                    // if this iv maps to the same gep, we have a collision
                    if( iv.second.find( gep ) != iv.second.end() )
                    {
                        // push this IV into the unordered set (we figure out order later)
                        unorderedVars.insert(iv.first);
                    }
                }
                if( unorderedVars.empty() )
                {
                    PrintVal(bp->getNode()->getVal());
                    for( const auto& iv : unorderedVars )
                    {
                        PrintVal(iv->getNode()->getVal());
                    }
                    PrintVal(gep);
                    for( const auto& iv : IVs )
                    {
                        PrintVal(iv->getNode()->getVal());
                    }
                    throw AtlasException("Could not find an IV for a dimension of a base pointer!");
                }
            }
            else
            {
                // TODO: constant offsets need to be remembered and dealt with somehow
            }
        }
        // now that we have the IVs that map to this BP, we need to order them in the dimension that they indexed the BP
        // there are commonly two cases here
        // 1. (optimized code) the vars used the same gep to index a bp
        //   - then the ordering is specified in the ordering of the operands in the GEP
        // 2. (unoptimized code) the vars came from different geps
        //   - then the ordering is implied by the ordering of the geps touching the BP
        vector<shared_ptr<InductionVariable>> vars;
        if( taskGeps.size() == 1 )
        {
            // optimized case
            auto targetGep = *taskGeps.begin();
            for( auto idx = targetGep->idx_begin(); idx != targetGep->idx_end(); idx++ )
            {
                // if the index is a constant we don't worry about it
                if( const auto& con = llvm::dyn_cast<llvm::Constant>(idx) )
                {
                    continue;
                }
                shared_ptr<InductionVariable> idxOp = nullptr;
                for( const auto& iv : unorderedVars )
                {
                    if( (idx->get() == iv->getNode()->getVal()) || (iv->isOffset(idx->get())) )
                    {
                        idxOp = iv;
                    }
                }
                if( idxOp == nullptr )
                {
                    PrintVal(targetGep);
                    for( const auto& iv : unorderedVars )
                    {
                        PrintVal(iv->getNode()->getVal());
                    }
                    throw AtlasException("Cannot find an IV that maps to this index operand in the target gep!");
                }
                vars.push_back(idxOp);
            }
        }
        else
        {
            // when there are multiple geps, there are two known cases to handle
            // 1. the loop has been partially unrolled
            //    - there are multiple geps that use the same IVs, one that may be getting offset by an affine expression
            //    - to find this case, we simply introspect the offsets of each gep, and if they both map to the same IVs, we actually have the 1-gep case (above)
            // 2. the geps are working together to offset the base pointer (commonly found when the optimizer is turned off)
            //    - the ordering of the geps is implied in the base pointer class. Thus we need to reference the loads/stores members to get the correct ordering
            map<const llvm::GetElementPtrInst*, set<shared_ptr<InductionVariable>>> indexMap;
            // to get the ordering back for the applicable geps, we make a vector of the task geps with the ordering implied from the base pointer loads/stores members
            vector<const llvm::GetElementPtrInst*> ordering;
            for( const auto& p : bp->getAccesses() )
            {
                if( taskGeps.find(p.first) != taskGeps.end() )
                {
                    ordering.push_back(p.first);
                }
            }
            for( const auto& p : bp->getStores() )
            {
                if( taskGeps.find(p.first) != taskGeps.end() )
                {
#ifdef DEBUG
                    if( std::find(ordering.begin(), ordering.end(), p.first) != ordering.end() )
                    {
                        PrintVal(bp->getNode()->getVal());
                        for( const auto& o : ordering )
                        {
                            PrintVal(o);
                        }
                        PrintVal(p.first);
                        throw AtlasException("Just tried to push the same gep into the ordering vector!");
                    }
#endif
                    ordering.push_back(p.first);
                }
            }
            // now we go through each gep and map each gep to the IV in its offsets
            for( const auto& gep : ordering )
            {
                for( auto idx = gep->idx_begin(); idx != gep->idx_end(); idx++ )
                {
                    shared_ptr<InductionVariable> idxOp = nullptr;
                    for( const auto& iv : unorderedVars )
                    {
                        if( iv->isOffset(idx->get()) )
                        {
                            indexMap[gep].insert(iv);
                        }
                    }
                }
            }
            // the map now contains each gep and the IVs they map to
            // if each gap maps to the same IVs then they are all the "same" operation
            set<set<const llvm::GetElementPtrInst*>> gepGroups;
            set<const llvm::GetElementPtrInst*> covered;
            for( const auto& gep : indexMap )
            {
                if( covered.find(gep.first) == covered.end() )
                {
                    set<const llvm::GetElementPtrInst*> newGroup;
                    newGroup.insert(gep.first);
                    covered.insert(gep.first);
                    // see if this already belongs to a group
                    for( const auto& gep2 : indexMap )
                    {
                        if( gep.first == gep2.first )
                        {
                            continue;
                        }
                        if( gep.second == gep2.second )
                        {
                            newGroup.insert(gep2.first);
                            covered.insert(gep2.first);
                        }
                    }
                    gepGroups.insert(newGroup);
                }
            }
            // now that geps with like IVs are grouped together, we can decide between each case
            // unrolled loop -> there is only one group
            // series of geps -> there is more than one group
            if( gepGroups.size() == 1 )
            {
                // since the geps use the same IVs we just use the beginning one
#ifdef DEBUG
                // some checks to see if the geps are the acting the same way
                // 1. same number of indices
                auto group = *gepGroups.begin();
                unsigned indices = (*group.begin())->getNumIndices();
                for( auto i = next( group.begin() ); i != group.end(); i++ )
                {
                    if( (*i)->getNumIndices() != indices )
                    {
                        throw AtlasException("Gep group members have differing index counts!");
                    }
                }
                // 2. same ordering of IVs
                /*vector<vector<const llvm::Value*>> idxes;
                for( auto gep : group )
                {
                    vector<const llvm::Value*> indices;
                    for( auto idx = gep->idx_begin(); idx != gep->idx_end(); idx++ )
                    {
                        indices.push_back(*idx);
                    }
                    idxes.push_back(indices);
                }
                for( unsigned i = 0; i < indices; i++ )
                {
                    auto idx = (*idxes.begin())[i];
                    for( auto entry : idxes )
                    {
                        if( entry[i] != idx )
                        {
                            throw AtlasException("Geps within gep group do not have the same ordering of indices!");
                        }
                    }
                }*/
#endif
                // get the ordering of vars from the gep and push them to the vars vector
                auto geps = *gepGroups.begin();
                // we evaluate each gep for its implied IV ordering
                set<vector<shared_ptr<InductionVariable>>> orders;
                for( const auto& gep : geps )
                {
                    orders.insert( getOrdering(gep, IVs) );
                }
                auto ref = *orders.begin();
#ifdef DEBUG
                for( const auto& vars : orders )
                {
                    for( unsigned i = 0; i < ref.size(); i++ )
                    {
                        if( vars[i] != ref[i] )
                        {
                            for( const auto& gep : geps )
                            {
                                PrintVal(gep);
                            }
                            throw AtlasException("Ordering of geps did not agree!");
                        }
                    }
                }
#endif
                vars = ref;
            }
            else if( gepGroups.size() > 1 )
            {
                throw AtlasException("Cannot handle the case where there are more than one gep group for a collection!");
            }
            else
            {
                PrintVal(bp->getNode()->getVal());
                for( const auto& ld : bp->getAccesses() )
                {
                    PrintVal(ld.first);
                }
                for( const auto& st : bp->getStores() )
                {
                    PrintVal(st.first);
                }
                throw AtlasException("Did not have any groups of geps!");
            }
        }
        // once we find the overlap of geps between IVs and BPs, we construct a collection for it
        auto newColl = make_shared<Collection>(bp, vars);
        colls.insert(newColl);
    }
    return colls;
}

/// @brief Creates expressions from collections and function nodes
///
/// Expressions use the collections of functions and the found collections of data to generate the rhs of a function
/// Steps:
/// 1. Group each functional expression together // find all rhs expressions
/// 2. For each function group // replace instructions in the rhs expression with collections
///    - for each instruction in the group
///      -- figure out which collection supplies this instruction (if any)
/// 3. Construct expressions from function
/// @param DG 
/// @param colls 
/// @return 
shared_ptr<Expression> getExpression(const shared_ptr<Task>& t, const set<shared_ptr<Collection>>& colls, const set<shared_ptr<ReductionVariable>>& rvs)
{
    shared_ptr<Expression> expr;
    // the following DFG walk does both 1 and 2
    // 1. Group all function nodes together
    deque<shared_ptr<Inst>> Q;
    set<shared_ptr<DataValue>, p_GNCompare> covered;
    set<shared_ptr<Inst>, p_GNCompare> functionGroup;
    // 2. find the loads that feed each group
    set<const llvm::LoadInst*> lds;
    // 3. find the stores that store each group
    set<const llvm::StoreInst*> sts;
    for( const auto& c : t->getCycles() )
    {
        for( const auto& block : c->getBody() )
        {
            for( const auto& node : block->instructions )
            {
                if( covered.find(node) == covered.end() )
                {
                    covered.insert(node);
                    if( node->isFunction() )
                    {
                        Q.clear();
                        Q.push_front(node);
                        functionGroup.insert(node);
                        while( !Q.empty() )
                        {
                            for( const auto& user : Q.front()->getInst()->users() )
                            {
                                // we expect eating stores to happen only after functional groups
                                if( const auto st = llvm::dyn_cast<llvm::StoreInst>(user) )
                                {
                                    sts.insert(st);
                                    covered.insert(DNIDMap.at(st));
                                }
                                else if( const auto userInst = llvm::dyn_cast<llvm::Instruction>(user) )
                                {
                                    if( covered.find(DNIDMap.at(userInst)) == covered.end() )
                                    {
                                        if( static_pointer_cast<Inst>(DNIDMap.at(userInst))->isFunction() )
                                        {
                                            functionGroup.insert(static_pointer_cast<Inst>(DNIDMap.at(userInst)));
                                        }
                                        covered.insert(DNIDMap.at(userInst));
                                        Q.push_back(static_pointer_cast<Inst>(DNIDMap.at(userInst)));
                                    }
                                }
                            }
                            for( const auto& use : Q.front()->getInst()->operands() )
                            {
                                // we expect feeding loads to happen only prior to functional groups
                                if( const auto ld = llvm::dyn_cast<llvm::LoadInst>(use) )
                                {
                                    lds.insert(ld);
                                    covered.insert(DNIDMap.at(ld));
                                }
                                else if( const auto useInst = llvm::dyn_cast<llvm::Instruction>(use) )
                                {
                                    if( covered.find(DNIDMap.at(useInst)) == covered.end() )
                                    {
                                        if( static_pointer_cast<Inst>(DNIDMap.at(useInst))->isFunction() )
                                        {
                                            functionGroup.insert(static_pointer_cast<Inst>(DNIDMap.at(useInst)));
                                        }
                                        covered.insert(DNIDMap.at(useInst));
                                        Q.push_back(static_pointer_cast<Inst>(DNIDMap.at(useInst)));
                                    }
                                }
                            }
                            Q.pop_front();
                        }
                        break;
                    }
                }
            }
        }
    }
    // 3. construct expressions
    // - walk the function group from start to finish
    //   -- for each datanode, construct an expression
    //      --- if the datanode is a reduction variable, construct a reduction
    // maps each node to expressions, which makes it convenient to construct expressions that use other expressions
    map<shared_ptr<DataValue>, shared_ptr<Expression>> nodeToExpr;

    // in order for expression generation to go well, ops need to be done in the right order (producer to consumer)
    // thus, the following logic attempts to order the instructions such that the instructions at the beginning of the group are done first
    // this way, the expressions that use earlier expressions have an expression to refer to
    // each binary operation in the function group is recorded in order, this will give us the operators in the expression
    vector<Cyclebite::Graph::Operation> ops;
    // in order to find the ordering of the group, we find the instruction that doesn't use any other instruction in the group
    // then we walk the DFG to find all subsequent instructions
    shared_ptr<Inst> first = nullptr;
    // there are two checks here
    // 1. the group forms a phi -> op(s) -> phi... cycle
    //    - in this case the phi is the first instruction, since it has the initial value of whatever variable we are dealing with
    // 2. the group does not form a cycle
    //    - then we loop for members of the group whose operands are all outside the group
    set<shared_ptr<UnconditionalEdge>, GECompare> edges;
    for( const auto& n : functionGroup )
    {
        for( const auto& p : n->getPredecessors() )
        {
            edges.insert(static_pointer_cast<UnconditionalEdge>(p));
        }
        for( const auto& s : n->getSuccessors() )
        {
            edges.insert(static_pointer_cast<UnconditionalEdge>(s));
        }
    }
    DataGraph dg(functionGroup, edges);
    if( Cyclebite::Graph::FindCycles(dg) ) // forms a cycle
    {
        // get the phi and set a user of it (that is within the function group) as the first instruction
        // the phi itself does not belong in the function group (because phis create false dependencies and are not important to the expression)
        set<const llvm::PHINode*> phis;
        for( const auto& n : functionGroup )
        {
            if( const auto phi = llvm::dyn_cast<llvm::PHINode>(n->getVal()) )
            {
                phis.insert(phi);
            }
        }
        if( phis.size() == 1 )
        {
            first = static_pointer_cast<Inst>(DNIDMap.at(*phis.begin()));
        }
        else
        {
#ifdef DEBUG
            for( const auto& n : functionGroup ) 
            {
                PrintVal(n->getVal());
            }
#endif
            throw AtlasException("Cannot handle the case where a cycle in the DFG contains multiple phis!");
        }
    }
    else
    {
        for( const auto& n : functionGroup )
        {
            bool allOutside = true;
            for( const auto& op : n->getInst()->operands() )
            {
                if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(op) )
                {
                    if( functionGroup.find(DNIDMap.at(inst)) != functionGroup.end() )
                    {
                        allOutside = false;
                        break;
                    }
                }
            }
            if( allOutside )
            {
                first = n;
                break;
            }
        }
    }
    if( !first )
    {
        throw AtlasException("Could not find first instruction in the instruction group!");
    }

    // now we need to find the order of operations
    vector<shared_ptr<Inst>> order;
    order.push_back(first);
    deque<const llvm::Instruction*> instQ;
    set<const llvm::Instruction*> instCovered;
    instQ.push_front(first->getInst());
    instCovered.insert(first->getInst());
    while(!instQ.empty() )
    {
        bool depthFirst = false;
        for( const auto& op : instQ.front()->operands() )
        {
            if( const auto& phi = llvm::dyn_cast<llvm::PHINode>(instQ.front()) )
            {
                // we don't look through phi operands because that will lead to the back of the cycle
                // and we prefer phis to be the first instruction in the ordering (because that is literally how the instructions will have executed)
                break;
            }
            else if( const auto& opInst = llvm::dyn_cast<llvm::Instruction>(op) )
            {
                if( instCovered.find(opInst) == instCovered.end() )
                {
                    if( functionGroup.find(DNIDMap.at(opInst)) != functionGroup.end() )
                    {
                        // this instruction comes before the current, thus two things need to happen
                        // 1. it needs to be pushed before the current instruction in the "order" list
                        // 2. its operands need to be investigated before anything else
                        auto pos = std::find(order.begin(), order.end(), DNIDMap.at(instQ.front()));
                        if( pos == order.end() )
                        {
                            throw AtlasException("Cannot resolve where to insert function inst operand in the ordered list!");
                        }
                        order.insert(pos, static_pointer_cast<Inst>(DNIDMap.at(opInst)));
                        instCovered.insert(opInst);
                        instQ.push_front(opInst);
                        depthFirst = true;
                        break;
                    }
                }
            }
        }
        if( depthFirst ) { continue; }
        for( const auto& user : instQ.front()->users() )
        {
            if( const auto& userInst = llvm::dyn_cast<llvm::Instruction>(user) )
            {
                if( instCovered.find(userInst) == instCovered.end() )
                {
                    if( functionGroup.find(DNIDMap.at(userInst)) != functionGroup.end() )
                    {
                        order.push_back(static_pointer_cast<Inst>(DNIDMap.at(userInst)));
                        instQ.push_back(userInst);
                        instCovered.insert(userInst);
                    }
                }
            }
        }
        instQ.pop_front();
    }
/*#ifdef DEBUG
    spdlog::info("Function group ordering:");
    for( const auto& inst : order )
    {
        PrintVal(inst->getVal());
    }
#endif*/

    // with the order in hand, we can construct the expression
    // first, choose whether this expression (or group of them) will become a reduction or not
    if( !rvs.empty() )
    {
        // make a quick mapping from datanode to reduction variable
        map<shared_ptr<DataValue>, shared_ptr<ReductionVariable>> dnToRv;
        for( const auto& node : order )
        {
            for( const auto& rv : rvs )
            {
                if( rv->getNode() == node )
                {
                    dnToRv[node] = rv;
                    break;
                }
            }
        }
        // a reduction should be a cycle starting at a phi, followed by ops (binary or cast), that ultimately feed a reduction variable
        // we must separate the phi from the binary ops from the RV, then construct the expression for the reduction (which is just the binary ops), then add the reduction to it (which sets the operator next to the equal sign e.g. "+=")
        const llvm::PHINode* phi = nullptr;
        vector<shared_ptr<Inst>> insts;
        shared_ptr<ReductionVariable> rv = nullptr;
        for( const auto& node : order )
        {
            if( const auto p = llvm::dyn_cast<llvm::PHINode>(node->getInst()) )
            {
                phi = p;
            }
            else if( dnToRv.find(node) != dnToRv.end() )
            {
                rv = dnToRv.at(node);
            }
            else
            {
                insts.push_back(node);
            }
        }
        for( const auto& node : insts )
        {
            // if it is a binary operation we need to take its operation
            if( const auto bin = llvm::dyn_cast<llvm::BinaryOperator>(node->getInst()) )
            {
                ops.push_back( Cyclebite::Graph::GetOp(bin->getOpcode()) );
            }
            vector<shared_ptr<Symbol>> vec;
            for( const auto& op : node->getInst()->operands() )
            {
                if( const auto inst = llvm::dyn_cast<llvm::Instruction>(op) )
                {
                    auto opNode = DNIDMap.at(inst);
                    if( std::find(insts.begin(), insts.end(), opNode) != insts.end() )
                    {
                        continue;
                    }
                    if( nodeToExpr.find(DNIDMap.at(inst)) != nodeToExpr.end() )
                    {
                        // this value comes from a previous function group operator, thus it should be a symbol in the expression
                        vec.push_back((nodeToExpr.at(DNIDMap.at(inst))));
                    }
                    else if( const auto ld = llvm::dyn_cast<llvm::LoadInst>(inst) )
                    {
                        // this value feeds the function group
                        // what we need to do is map the load to the collection that represents the value it loads
                        // this collection will become the symbol in the expression
                        shared_ptr<Collection> found = nullptr;
                        for( const auto& coll : colls )
                        {
                            auto lds = coll->getBP()->getlds();
                            if( std::find(lds.begin(), lds.end(), ld) != lds.end() )
                            {
                                found = coll;
                                break;
                            }
                        }
                        if( found )
                        {
                            vec.push_back(found);
                        }
                    }
                    else if( const auto st = llvm::dyn_cast<llvm::StoreInst>(inst) )
                    {
                        // this instruction stores the function group result, do nothing for now
                    }
                    else if( const auto bin = llvm::dyn_cast<llvm::BinaryOperator>(inst) )
                    {
                        // a binary op in a series of ops
                        // there should be an expression for the input(s) of this op
                        if( nodeToExpr.find(opNode) != nodeToExpr.end() )
                        {
                            vec.push_back(nodeToExpr.at(opNode));
                        }
                        else
                        {
                            PrintVal(bin);
                            PrintVal(opNode->getVal());
                            throw AtlasException("Cannot map this instruction to an expression!");
                        }
                    }
                }
                else if( auto con = llvm::dyn_cast<llvm::Constant>(op) )
                {
                    if( con->getType()->isIntegerTy() )
                    {
                        vec.push_back(make_shared<ConstantSymbol>(*con->getUniqueInteger().getRawData()));
                    }
                    else
                    {
                        PrintVal(op);
                        PrintVal(node->getVal());
                        throw AtlasException("Constant used in an expression is not an integer!");
                    }
                }
                else
                {
                    PrintVal(op);
                    PrintVal(node->getVal());
                    throw AtlasException("Cannot recognize this operand type when building an expression!");
                }
            }
            expr = make_shared<Reduction>(rv, vec, ops);
            nodeToExpr[node] = expr;
            nodeToExpr[rv->getNode()] = expr;        
        }
    }
    else
    {
        for( const auto& node : order )
        {
            // if it is a binary operation we need to take its operation
            if( const auto bin = llvm::dyn_cast<llvm::BinaryOperator>(node->getInst()) )
            {
                ops.push_back( Cyclebite::Graph::GetOp(bin->getOpcode()) );
            }
            vector<shared_ptr<Symbol>> vec;
            for( const auto& op : node->getInst()->operands() )
            {
                if( const auto inst = llvm::dyn_cast<llvm::Instruction>(op) )
                {
                    auto opNode = DNIDMap.at(inst);
                    if( nodeToExpr.find(DNIDMap.at(inst)) != nodeToExpr.end() )
                    {
                        // this value comes from a previous function group operator, thus it should be a symbol in the expression
                        vec.push_back((nodeToExpr.at(DNIDMap.at(inst))));
                    }
                    else if( const auto ld = llvm::dyn_cast<llvm::LoadInst>(inst) )
                    {
                        // this value feeds the function group
                        // what we need to do is map the load to the collection that represents the value it loads
                        // this collection will become the symbol in the expression
                        shared_ptr<Collection> found = nullptr;
                        for( const auto& coll : colls )
                        {
                            auto lds = coll->getBP()->getlds();
                            if( std::find(lds.begin(), lds.end(), ld) != lds.end() )
                            {
                                found = coll;
                                break;
                            }
                        }
                        if( found )
                        {
                            vec.push_back(found);
                        }
                    }
                    else if( const auto st = llvm::dyn_cast<llvm::StoreInst>(inst) )
                    {
                        // this instruction stores the function group result, do nothing for now
                    }
                    else if( const auto bin = llvm::dyn_cast<llvm::BinaryOperator>(inst) )
                    {
                        // a binary op in a series of ops
                        // there should be an expression for the input(s) of this op
                        if( nodeToExpr.find(opNode) != nodeToExpr.end() )
                        {
                            vec.push_back(nodeToExpr.at(opNode));
                        }
                        else
                        {
                            PrintVal(bin);
                            PrintVal(opNode->getVal());
                            throw AtlasException("Cannot map this instruction to an expression!");
                        }

                    }
                }
                else if( auto con = llvm::dyn_cast<llvm::Constant>(op) )
                {
                    if( con->getType()->isIntegerTy() )
                    {
                        vec.push_back(make_shared<ConstantSymbol>(*con->getUniqueInteger().getRawData()));
                    }
                    else if( const auto& func = llvm::dyn_cast<llvm::Function>(con) )
                    {
                        // we have found a function call that returns a value, more generall a "global object" in the llvm api
                        // here we implement a whitelist that allows certain functions that we know we can handle
                        // for example; rand(), sort(), qsort() and other libc function calls are on the whitelist
                        // for now we just insert the function name and hope for the best
                        vec.push_back(make_shared<ConstantFunction>(func));
                    }
                    else
                    {
                        PrintVal(op);
                        PrintVal(node->getVal());
                        throw AtlasException("Constant used in an expression is not an integer!");
                    }
                }
                else
                {
                    PrintVal(op);
                    PrintVal(node->getVal());
                    throw AtlasException("Cannot recognize this operand type when building an expression!");
                }
            }
            expr = make_shared<Expression>(vec, ops);
            nodeToExpr[node] = expr;
        }
    }
    return expr;
}

/*set<shared_ptr<Function>> getFunctions( const set<shared_ptr<Expression>>& exprs)
{
    set<shared_ptr<Function>> funcs;
    // there is a 1:1 mapping between function and high-level expression, which are the expressions in the expr arg
    for( const auto& expr : exprs )
    {
        // each expression represents an operation on two data elements
        // the parallelism of these elements is determined by their position in the polyhedral space, defined by the collections in the expression
        // to find parallelism between expressions
    }
    return funcs;
}*/

void Cyclebite::Grammar::Process(const set<shared_ptr<Task>>& tasks)
{
    // each expression maps 1:1 with tasks from the cartographer
    unsigned TID = 0;
    for( const auto& t : tasks )
    {
        try
        {
#ifdef DEBUG
            cout << endl;
            spdlog::info("Task "+to_string(TID++));
#endif
            // get all induction variables
            auto vars = getInductionVariables(t);
            // get all reduction variables
            auto rvs = getReductionVariables(t, vars);
            // get all base pointers
            auto bps  = getBasePointers(t);
            // construct collections
            auto cs   = getCollections(t, vars, bps);
            // each task should have exactly one expression
            auto expr = getExpression(t, cs, rvs);
#ifdef DEBUG
            spdlog::info("Vars:");
            for( const auto& var : vars )
            {
                spdlog::info(var->dump()+" -> "+PrintVal(var->getNode()->getVal(), false));
            }
            spdlog::info("Reductions");
            for( const auto& rv : rvs )
            {
                spdlog::info(rv->dump()+" -> "+PrintVal(rv->getNode()->getVal(), false));
            }
            spdlog::info("Base Pointers");
            for( const auto& bp : bps )
            {
                spdlog::info(bp->dump()+" -> "+PrintVal(bp->getNode()->getVal(), false));
            }
            spdlog::info("Collections:");
            for( const auto& c : cs )
            {
                spdlog::info(c->dump());
            }
            spdlog::info("Expression:");
            spdlog::info(expr->dump());
            cout << endl;
#endif
        }
        catch(AtlasException& e)
        {
            spdlog::critical(e.what());
#ifdef DEBUG
            cout << endl;
#endif
        }
    }
}