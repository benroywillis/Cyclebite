//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Categorize.h"
#include "Task.h"
#include <deque>
#include "Graph/inc/IO.h"
#include "Util/IO.h"
#include "Util/Exceptions.h"

using namespace Cyclebite::Grammar;
using namespace std;

/// @brief Finds the instructions that carry out the function of a kernel
///
/// First pass: find the values that are stored (in both addresses ie llvm::StoreInst and registers ie llvm::PHINode)
///             and walk their fan-in until a load (in both addresses ie llvm::LoadInst and registers ie llvm::PHINode) is hit, color all touched nodes red
/// Second pass: for all loads found in the first pass, walk their fan-out until a store is hit, color all touched nodes blue
/// Red: values that are stored and did not come from a load
/// Blue: values that are loaded but do not get stored
/// Red&&Blue: values that are both loaded and stored (this is the "function" of the kernel)
set<shared_ptr<Cyclebite::Graph::DataValue>> findFunction(const set<shared_ptr<Task>>& tasks)
{
    set<shared_ptr<Cyclebite::Graph::DataValue>> funcs;
    // colors for each instruction found during DFG investigation
    set<shared_ptr<NodeColor>, NCCompare> colors;
    for (const auto& t : tasks)
    {
        // set of ld instructions that were the first lds seen when walking back from sts
        set<const llvm::Instruction *> lds;
        // set of sts that receive kernel function fan-out
        set<const llvm::Instruction *> sts;
        // First pass
        for( const auto& c : t->getCycles() )
        {
            for( const auto& b : c->getBody() )
            {
                for( const auto& i : b->getInstructions() )
                {
                    if (const auto& st = llvm::dyn_cast<llvm::StoreInst>(i->getInst()))
                    {
                        // walk fan-in to store
                        // we explore users until we find a gep or ld
                        deque<const llvm::Instruction *> Q;
                        set<const llvm::Value *> covered;
                        if (auto inst = llvm::dyn_cast<llvm::Instruction>(st->getValueOperand()))
                        {
                            auto r = colors.insert(make_shared<NodeColor>(inst, OpColor::Red));
                            if (!r.second)
                            {
                                (*r.first)->colors.insert(OpColor::Red);
                            }
                            Q.push_front(inst);
                            covered.insert(inst);
                        }
                        else
                        {
                            continue;
                        }
                        while (!Q.empty())
                        {
                            if (auto ld = llvm::dyn_cast<llvm::LoadInst>(Q.front()))
                            {
                                // regular case when the results of a function group are stored in a pointer (the heap)
                                lds.insert(ld);
                            }
                            else if( auto phi = llvm::dyn_cast<llvm::PHINode>(Q.front()) )
                            {
                                // in the case where results of a function group are stored in a register, this captures them
                                lds.insert(phi);
                            }
                            else if( auto call = llvm::dyn_cast<llvm::CallBase>(Q.front()) )
                            {
                                // a node that is only read may be a function that only returned a value, like libc::rand()
                                // thus, if it is a function we consider it part of the function group
                                lds.insert(call);
                                // unlike the other types of lds, for all we know a function does work itself (not just a memory transaction)
                                // thus it is colored
                                auto r = colors.insert(make_shared<NodeColor>(call, OpColor::Red));
                                if (!r.second)
                                {
                                    (*r.first)->colors.insert(OpColor::Red);
                                }
                                // the call instruction needs to be colored twice to ensure it is in the function group
                                (*r.first)->colors.insert(OpColor::Blue);
                                for( const auto& op : Q.front()->operands() )
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
                            else if (auto st = llvm::dyn_cast<llvm::StoreInst>(Q.front()))
                            {
                                // a store can't possibly be used in a store... something is wrong
                                throw CyclebiteException("Found a store that is an operand to a store!");
                            }
                            else if (auto inst = llvm::dyn_cast<llvm::Instruction>(Q.front()))
                            {
                                auto r = colors.insert(make_shared<NodeColor>(inst, OpColor::Red));
                                if (!r.second)
                                {
                                    (*r.first)->colors.insert(OpColor::Red);
                                }
                                for( const auto& op : Q.front()->operands() )
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
                            Q.pop_front();
                        }
                        sts.insert(st);
                    }
                }
            }
        }
        // Second pass
        // works well for tasks that have an input working set
        for (const auto& ld : lds)
        {
            // walk fan-out from ld
            // we explore users until we run out of instructions, coloring everything blue as we go
            deque<const llvm::Instruction *> Q;
            set<const llvm::User *> covered;
            Q.push_front(ld);
            covered.insert(ld);
            while (!Q.empty())
            {
                for ( const auto& u : Q.front()->users())
                {
                    if( covered.contains(u) )
                    {
                        continue;
                    }
                    else if ( const auto& st = llvm::dyn_cast<llvm::StoreInst>(u))
                    {
                        // stores mark the end of the possible function instructions
                        continue;
                    }
                    else if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(u) )
                    {
                        // we don't consider loads for function group membership, so skip
                        continue;
                    }
                    else if( const auto& gep = llvm::dyn_cast<llvm::GetElementPtrInst>(u) )
                    {
                        // we also don't consider loads for funtion group membership either
                        continue;
                    }
                    else if (const auto& inst = llvm::dyn_cast<llvm::Instruction>(u))
                    {
                        auto r = colors.insert(make_shared<NodeColor>(inst, OpColor::Blue));
                        if (!r.second)
                        {
                            (*r.first)->colors.insert(OpColor::Blue);
                        }
                        if( covered.find(inst) == covered.end() )
                        {
                            Q.push_back(inst);
                        }
                    }
                    covered.insert(u);
                }
                if (!Q.empty())
                {
                    Q.pop_front();
                }
            }
        }
    }
    for (const auto &entry : colors)
    {
        if (entry->colors.contains(OpColor::Red))
        {
            if (entry->colors.contains(OpColor::Blue))
            {
                funcs.insert( Cyclebite::Graph::DNIDMap.at(entry->inst) );
            }
        }
    }
    return funcs;
}

/// @brief Colors nodes that use, compute and store state
///
/// First pass: colors Red all instructions that fan-in to stateful instructions (call, ret, br, jmp)
/// Second pass: identifies the instructions that calculate new state
/// Red: values that determine the next state
/// Blue: state values that are stored
/// Red&&Blue: values that are both used to determine the next state and are stored
/// Any node that has a color belongs in the "state" box
set<shared_ptr<Cyclebite::Graph::DataValue>> findState(const set<shared_ptr<Task>>& tasks)
{
    set<shared_ptr<Cyclebite::Graph::DataValue>> state;
    // colors for each instruction found during DFG investigation
    set<shared_ptr<NodeColor>, NCCompare> colors;
    for ( const auto& t : tasks )
    {
        // we are interested in finding the instructions that are used to determine whether to exit the current cycle
        // inside of the task is a set of instruction(s) that can either enter or exit each cycle
        // these are our starting points when finding state instructions

        // set of store instructions that store state (these stores use the same pointer used to load state)
        set<const llvm::StoreInst *> stateSts;
        // this set holds all instructions that determine a state. Their operands likely lead back to a value that stores state - an induction variable
        set<const llvm::Instruction*> targets;
        for( const auto& c : t->getCycles() )
        {
            for( const auto& b : c->getBody() )
            {
                for( const auto& i : b->getInstructions() )
                {
                    // First pass finds the induction variables in the program (that is, across all tasks)
                    if (i->getInst()->isTerminator())
                    {
                        // target value that determines a state
                        // this value leads back to an origin somewhere, likely a load, and this code is supposed to find it
                        if ( i->isTerminator() )
                        {
                            // we check the successors of this instruction. If one is outside the cycle and one in inside, we know this is recur-logic
                            if( b->getSuccessors().size() > 1 )
                            {
                                bool hasInside = false;
                                bool hasOutside = false;
                                for( const auto& succ : b->getSuccessors() )
                                {
                                    if( c->find( static_pointer_cast<Cyclebite::Graph::ControlBlock>(succ->getSnk())) )
                                    {
                                        hasInside = true;
                                    }
                                    else
                                    {
                                        hasOutside = true;
                                    }
                                }
                                if( hasInside && hasOutside )
                                {
                                    targets.insert(i->getInst());
                                }
                            }
                        }
                        else
                        {
                            throw CyclebiteException("This terminator is not yet supported: " + PrintVal(i->getInst(), false));
                        }
                    }
                }
            }
        }
        // now that we have a values ("targets") that determines a new state, we walk backward through the DFG (starting at "target"'s operands) until 
        // 1. we find the original ld instruction that accesses the induction variable on the heap (in the case of unoptimized code)
        // 2. we find a cycle between a binary op and a PHINode (found in optimized code when the IV lives inside a value not the heap)
        // set of values that point to values used to determine the behavior of terminators (induction variables)
        set<const llvm::Value *> stateP;
        for( const auto& target : targets )
        {
            deque<const llvm::Instruction *> Q;
            set<const llvm::Value *> covered;
            auto r = colors.insert(make_shared<NodeColor>(target, OpColor::Red));
            if (!r.second)
            {
                (*r.first)->colors.insert(OpColor::Red);
            }
            Q.push_front(target);
            covered.insert(target);
            while (!Q.empty())
            {
                for (auto &u : Q.front()->operands())
                {
                    if (covered.find(u.get()) != covered.end())
                    {
                        continue;
                    }
                    else if( auto phi = llvm::dyn_cast<llvm::PHINode>(u.get()) )
                    {
                        // we know for certain that a state-changing instruction uses this value (or a transformation of it) therefore we designate it state
                        covered.insert(phi);
                        auto r = colors.insert(make_shared<NodeColor>(phi, OpColor::Red));
                        if (!r.second)
                        {
                            (*r.first)->colors.insert(OpColor::Red);
                        }
                    }
                    else if( auto ld = llvm::dyn_cast<llvm::LoadInst>(u.get()) )
                    {
                        // case found in unoptimized programs when the induction variable lives on the heap (not in a value) and is communicated with through ld/st
                        // the pointer argument to this load is likely the induction variable pointer, so add that to the stateP set
                        covered.insert(ld);
                        stateP.insert( ld->getPointerOperand() );
                    }
                    else if (auto st = llvm::dyn_cast<llvm::StoreInst>(u.get()))
                    {
                        // a store can't possibly be used in a store... something is wrong
                        throw CyclebiteException("Found a store that affected state!");
                    }
                    else if (auto inst = llvm::dyn_cast<llvm::Instruction>(u.get()))
                    {
                        auto r = colors.insert(make_shared<NodeColor>(inst, OpColor::Red));
                        if (!r.second)
                        {
                            (*r.first)->colors.insert(OpColor::Red);
                        }
                        Q.push_back(inst);
                    }
                    covered.insert(u.get());
                }
                if (!Q.empty())
                {
                    Q.pop_front();
                }
            }
        }
        // Second pass, colors nodes blue
        // this pass takes each pointer that was dereferenced to determine state, finds load instructions that use that pointer, and follows those paths to see what they did to the value
        // this can find the "functions" that were used to determine state (like induction variables and pointer offsets)
        for (const auto &p : stateP)
        {
            for( const auto& u : p->users() )
            {
                if( const auto& st = llvm::dyn_cast<llvm::StoreInst>(u) )
                {
                    stateSts.insert(st);
                }
            }
        }
        // for each store instruction that stores state
        for (const auto &st : stateSts)
        {
            if (auto valueInst = llvm::dyn_cast<llvm::Instruction>(st->getValueOperand()))
            {
                deque<const llvm::Instruction *> Q;
                set<const llvm::Value *> covered;
                auto r = colors.insert(make_shared<NodeColor>(valueInst, OpColor::Blue));
                if (!r.second)
                {
                    (*r.first)->colors.insert(OpColor::Blue);
                }
                Q.push_front(valueInst);
                covered.insert(valueInst);
                while (!Q.empty())
                {
                    for (auto &u : Q.front()->operands())
                    {
                        auto v = u.get();
                        if (covered.find(v) != covered.end())
                        {
                            continue;
                        }
                        else if (auto ld = llvm::dyn_cast<llvm::LoadInst>(v))
                        {
                            continue;
                        }
                        else if (auto inst = llvm::dyn_cast<llvm::Instruction>(v))
                        {
                            auto r = colors.insert(make_shared<NodeColor>(inst, OpColor::Blue));
                            if (!r.second)
                            {
                                (*r.first)->colors.insert(OpColor::Blue);
                            }
                            Q.push_back(inst);
                        }
                        covered.insert(v);
                    }
                    if (!Q.empty())
                    {
                        Q.pop_front();
                    }
                }
            }
        }
    }
    for (const auto &entry : colors)
    {
        if (!entry->colors.empty())
        {
            state.insert(Cyclebite::Graph::DNIDMap.at(entry->inst));
        }
    }
    return state;
}

/// @brief Identifies all instructions that access memory or manipulate memory accesses
///
/// First pass: for each memory instruction, walk the fan-in to their pointers and mark all those instructions blue
set<shared_ptr<Cyclebite::Graph::DataValue>> findMemory(const set<shared_ptr<Task>>& tasks)
{
    set<shared_ptr<Cyclebite::Graph::DataValue>> mem;
    // colors for each instruction found during DFG investigation
    set<shared_ptr<NodeColor>, NCCompare> colors;
    for( const auto& t : tasks )
    {
        set<const llvm::LoadInst *> lds;
        set<const llvm::StoreInst *> sts;
        for( const auto& c : t->getCycles() )
        {
            for( const auto& b : c->getBody() )
            {
                for( const auto& i : b->getInstructions() )
                {
                    if (auto ld = llvm::dyn_cast<llvm::LoadInst>(i->getInst()))
                    {
                        lds.insert(ld);
                    }
                    else if (auto st = llvm::dyn_cast<llvm::StoreInst>(i->getInst()))
                    {
                        sts.insert(st);
                    }
                }
            }
        }
        // First Pass
        deque<const llvm::Instruction *> Q;
        set<const llvm::Value *> covered;
        for( const auto& ld : lds )
        {
            if( const auto& ptr = llvm::dyn_cast<llvm::Instruction>(ld->getPointerOperand()) )
            {
                Q.push_back(ptr);
                covered.insert(ptr);
                auto r = colors.insert(make_shared<NodeColor>(ld, OpColor::Blue));
                if (!r.second)
                {
                    (*r.first)->colors.insert(OpColor::Blue);
                }
            }
        }
        for( const auto& st : sts )
        {
            if( const auto& ptr =  llvm::dyn_cast<llvm::Instruction>(st->getPointerOperand()) )
            {
                Q.push_back(ptr);
                covered.insert(ptr);
                auto r = colors.insert(make_shared<NodeColor>(st, OpColor::Blue));
                if (!r.second)
                {
                    (*r.first)->colors.insert(OpColor::Blue);
                }
            }
        }
        while (!Q.empty())
        {
            auto r = colors.insert(make_shared<NodeColor>(Q.front(), OpColor::Blue));
            if (!r.second)
            {
                (*r.first)->colors.insert(OpColor::Blue);
            }
            for (auto &u : Q.front()->operands())
            {
                auto v = u.get();
                if (covered.find(v) != covered.end())
                {
                    continue;
                }
                else if (auto inst = llvm::dyn_cast<llvm::Instruction>(v))
                {
                    Q.push_back(inst);
                }
                covered.insert(v);
            }
            if (!Q.empty())
            {
                Q.pop_front();
            }
        }
    }

    set<int64_t> memInstructions;
    for (const auto &entry : colors)
    {
        if (!entry->colors.empty())
        {
            mem.insert(Cyclebite::Graph::DNIDMap.at(entry->inst));
        }
    }
    return mem;
}

void Cyclebite::Grammar::colorNodes( const set<shared_ptr<Task>>& tasks )
{
    auto func  = findFunction(tasks);
    auto state = findState(tasks);
    auto mem   = findMemory(tasks);
    // one characteristic about the categories is that each instruction may only belong to one category
    // the current graph coloring algorithms don't consider who is in which category at all, thus we do the exclusion algorithm here
    // Priority of categories:
    // 1. State
    // 2. Function
    // 3. Memory
    for( const auto& s : state )
    {
        func.erase(s);
        mem.erase(s);
    }
    for( const auto& m : mem )
    {
        func.erase(m);
    }
    // now update all the nodes that were assigned to their respective category
    for( const auto& s : state )
    {
        static_pointer_cast<Cyclebite::Graph::Inst>(s)->setColor(Cyclebite::Graph::DNC::State);
    }
    for( const auto& m : mem )
    {
        static_pointer_cast<Cyclebite::Graph::Inst>(m)->setColor(Cyclebite::Graph::DNC::Memory);
    }
    for( const auto& f : func )
    {
        static_pointer_cast<Cyclebite::Graph::Inst>(f)->setColor(Cyclebite::Graph::DNC::Function);
    }
}