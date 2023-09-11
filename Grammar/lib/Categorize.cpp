#include "Categorize.h"
#include "Function.h"
#include <deque>
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
set<int64_t> Cyclebite::Grammar::findFunction(const map<string, set<llvm::BasicBlock *>> &kernelSets)
{
    // colors for each instruction found during DFG investigation
    set<shared_ptr<NodeColor>, NCCompare> colors;
    for (auto k : kernelSets)
    {
        // set of ld instructions that were the first lds seen when walking back from sts
        set<llvm::Instruction *> lds;
        // set of sts that receive kernel function fan-out
        set<llvm::Instruction *> sts;
        // First pass
        for (auto bb : k.second)
        {
            for (auto i = bb->begin(); i != bb->end(); i++)
            {
                if (auto st = llvm::dyn_cast<llvm::StoreInst>(i))
                {
                    // walk fan-in to store
                    // we explore users until we find a gep or ld
                    // nodes that precede a store before a gep or ld are always interesting
                    set<llvm::Instruction *> localFanIn;
                    deque<llvm::Instruction *> Q;
                    set<llvm::Value *> covered;
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
                        for (auto &u : Q.front()->operands())
                        {
                            auto v = u.get();
                            if (covered.find(v) != covered.end())
                            {
                                continue;
                            }
                            else if (auto ld = llvm::dyn_cast<llvm::LoadInst>(v))
                            {
                                // regular case when the results of a function group are stored in a pointer (the heap)
                                lds.insert(ld);
                            }
                            else if( auto phi = llvm::dyn_cast<llvm::PHINode>(v) )
                            {
                                // in the case where results of a function group are stored in a register, this captures them
                                lds.insert(phi);
                            }
                            else if( auto call = llvm::dyn_cast<llvm::CallBase>(v) )
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
                                Q.push_back(call);
                            }
                            else if (auto st = llvm::dyn_cast<llvm::StoreInst>(v))
                            {
                                // a store can't possibly be used in a store... something is wrong
                                throw AtlasException("Found a store that is an operand to a store!");
                            }
                            else if (auto inst = llvm::dyn_cast<llvm::Instruction>(v))
                            {
                                auto r = colors.insert(make_shared<NodeColor>(inst, OpColor::Red));
                                if (!r.second)
                                {
                                    (*r.first)->colors.insert(OpColor::Red);
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
                    sts.insert(st);
                }
            }
        }
        // Second pass
        // works well for tasks that have an input working set
        for (const auto& ld : lds)
        {
            // walk fan-out from ld
            // we explore users until we run out of instructions, coloring everything blue as we go
            deque<llvm::Instruction *> Q;
            set<llvm::User *> covered;
            Q.push_front(ld);
            covered.insert(ld);
            while (!Q.empty())
            {
                for ( const auto& u : Q.front()->users())
                {
                    if (covered.find(u) != covered.end())
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
    set<int64_t> funcInstructions;
    for (const auto &entry : colors)
    {
        if (entry->colors.find(OpColor::Red) != entry->colors.end())
        {
            if (entry->colors.find(OpColor::Blue) != entry->colors.end())
            {
                funcInstructions.insert(GetValueID(entry->inst));
            }
            else
            {

            }
        }
    }
    return funcInstructions;
}

/// @brief Colors nodes that use, compute and store state
///
/// First pass: colors Red all instructions that fan-in to stateful instructions (call, ret, br, jmp)
/// Second pass: identifies the instructions that calculate new state
/// Red: values that determine the next state
/// Blue: state values that are stored
/// Red&&Blue: values that are both used to determine the next state and are stored
/// Any node that has a color belongs in the "state" box
set<int64_t> Cyclebite::Grammar::findState(const map<string, set<llvm::BasicBlock *>> &kernelSets)
{
    // colors for each instruction found during DFG investigation
    set<shared_ptr<NodeColor>, NCCompare> colors;
    for (auto k : kernelSets)
    {
        // set of load instructions found in the kernel DFG
        set<llvm::LoadInst *> lds;
        // set of load instructions that load state (these loads were later used to determine the next state)
        set<llvm::LoadInst *> stateLds;
        // set of store instructions that store state (these stores use the same pointer used to load state)
        set<llvm::StoreInst *> stateSts;
        // set of terminator instructions that were used to determine a new state
        set<llvm::Instruction *> terms;
        // set of values that point to values used to determine the behavior of terminators (induction variables)
        set<llvm::Value *> stateP;

        // this set holds all instructions that determine a state. Their operands likely lead back to a value that stores state - an induction variable
        set<llvm::Instruction*> targets;
        // First pass finds the induction variables in the program (that is, across all tasks)
        for (auto bb : k.second)
        {
            for (auto i = bb->begin(); i != bb->end(); i++)
            {
                if (auto inst = llvm::dyn_cast<llvm::Instruction>(i))
                {
                    if (auto ld = llvm::dyn_cast<llvm::LoadInst>(inst))
                    {
                        lds.insert(ld);
                    }
                    else if (inst->isTerminator())
                    {
                        // target value that determines a state
                        // this value leads back to an origin somewhere, likely a load, and this code is supposed to find it
                        llvm::Instruction *target = nullptr;
                        if (auto br = llvm::dyn_cast<llvm::BranchInst>(inst))
                        {
                            //if (br->isConditional())
                            //{
                                //if (auto cond = llvm::dyn_cast<llvm::Instruction>(br->getCondition()))
                                //{
                                    target = br;
                                //}
                            //}
                        }
                        else if (auto se = llvm::dyn_cast<llvm::SelectInst>(inst))
                        {
                            //if (se->getNumSuccessors() > 1)
                            //{
                                //if (auto cond = llvm::dyn_cast<llvm::Instruction>(se->getCondition()))
                                //{
                                    //target = cond;
                                    target = se;
                                //}
                            //}
                        }
                        else if (auto sw = llvm::dyn_cast<llvm::SwitchInst>(inst))
                        {
                            //if (auto cond = llvm::dyn_cast<llvm::Instruction>(sw->getCondition()))
                            //{
                                //target = cond;
                                target = sw;
                            //}
                        }
                        else if (auto ret = llvm::dyn_cast<llvm::ReturnInst>(inst))
                        {
                            // ignore returns because they are unconditional
                        }
                        else if( auto invoke = llvm::dyn_cast<llvm::InvokeInst>(inst) )
                        {
                            // this is a function call with two possible return destinations, but the logic to choose which destination is hidden within the called function (the exception handling logic)
                            // thus we are stuck with a problem: we have possibly many conditions that can lead to the unwind destination
                            // this is a generalization of a conditional branch instruction
                            // for now (12/19/22) we are making the invoke instruction the target, thus downstream processing will select the function args as a
                            target = invoke;
                        }
                        else if( auto res = llvm::dyn_cast<llvm::ResumeInst>(inst) )
                        {
                            // this is a return from a function after an exception has been thrown
                            // so we ignore it just like a return instruction because they are "unconditional"
                        }
                        else if( auto unrea = llvm::dyn_cast<llvm::UnreachableInst>(inst) )
                        {
                            // this can be found in unoptimized code
                            // for now we ignore it because its not conditional
                        }
                        else
                        {
                            throw AtlasException("This terminator is not yet supported: " + PrintVal(inst, false));
                        }
                        if (target)
                        {
                            targets.insert(target);
                        }
                    }
                }
            }
        }
        // now that we have a values ("targets") that determines a new state, we walk backward through the DFG (starting at "target"'s operands) until 
        // 1. we find the original ld instruction that accesses the induction variable on the heap (in the case of unoptimized code)
        // 2. we find a cycle between a binary op and a PHINode (found in optimized code when the IV lives inside a value not the heap)
        for( const auto& target : targets )
        {
            set<llvm::Instruction *> localFanIn;
            deque<llvm::Instruction *> Q;
            set<llvm::Value *> covered;
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
                        //stateP.insert(phi);              
                        /*if( auto bin = llvm::dyn_cast<llvm::BinaryOperator>(Q.front()) )
                        {
                            // we have found a cycle between a binary op and a phi, likely indicating an induction variable, thus add it to the set of dimensions
                            covered.insert(bin);
                            covered.insert(phi);
                            //stateP.insert( phi );
                        }*/
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
                        throw AtlasException("Found a store that affected state!");
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
            terms.insert(target);
        }
        // Second pass, colors nodes blue
        // this pass takes each pointer that was dereferenced to determine state, finds load instructions that use that pointer, and follows those paths to see what they did to the value
        // this can find the "functions" that were used to determine state (like induction variables and pointer offsets)
        for (const auto &p : stateP)
        {
            // state commonly comes from instructions, but it can also come from constants (like static globals), so we introspect users to account for different cases
            if (auto user = llvm::dyn_cast<llvm::User>(p))
            {
                // collect all store instructions that store state
                set<llvm::Instruction *> localFanOut;
                deque<llvm::Instruction *> Q;
                set<llvm::User *> covered;
                for (const auto &u : user->users())
                {
                    if (auto st = llvm::dyn_cast<llvm::StoreInst>(u))
                    {
                        if (k.second.find(st->getParent()) != k.second.end())
                        {
                            stateSts.insert(st);
                        }
                    }
                }
            }
            else
            {
                throw AtlasException("Cannot handle the case where a state pointer comes from: " + PrintVal(p, false));
            }
        }
        // for each store instruction that stores state
        for (const auto &st : stateSts)
        {
            if (auto valueInst = llvm::dyn_cast<llvm::Instruction>(st->getValueOperand()))
            {
                // walk fan-in to state store (data only)
                set<llvm::Instruction *> localFanIn;
                deque<llvm::Instruction *> Q;
                set<llvm::Value *> covered;
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
    set<int64_t> stateInstructions;
    for (const auto &entry : colors)
    {
        if (!entry->colors.empty())
        {
            stateInstructions.insert(GetValueID(entry->inst));
        }
    }
    return stateInstructions;
}

/// @brief Identifies all instructions that access memory or manipulate memory accesses
///
/// First pass: for each memory instruction, walk the fan-in to their pointers and mark all those instructions blue
set<int64_t> Cyclebite::Grammar::findMemory(const map<string, set<llvm::BasicBlock *>> &kernelSets)
{
    // colors for each instruction found during DFG investigation
    set<shared_ptr<NodeColor>, NCCompare> colors;
    for (const auto &k : kernelSets)
    {
        set<llvm::LoadInst *> lds;
        set<llvm::StoreInst *> sts;
        // Before any passes find all loads and stored
        for (const auto &b : k.second)
        {
            for (auto i = b->begin(); i != b->end(); i++)
            {
                if (auto ld = llvm::dyn_cast<llvm::LoadInst>(i))
                {
                    lds.insert(ld);
                }
                else if (auto st = llvm::dyn_cast<llvm::StoreInst>(i))
                {
                    sts.insert(st);
                }
            }
        }
        // First Pass
        deque<llvm::Instruction *> Q;
        set<llvm::Value *> covered;
        // for each load pointer
        /*for (const auto &ld : lds)
        {
            if (auto valueInst = llvm::dyn_cast<llvm::Instruction>(ld->getPointerOperand()))
            {
                // walk fan-in to state store (data only)
                auto r = colors.insert(make_shared<NodeColor>(valueInst, OpColor::Blue));
                if (!r.second)
                {
                    (*r.first)->colors.insert(OpColor::Blue);
                }
                Q.push_front(valueInst);
                covered.insert(valueInst);
            }
        }
        // for each store pointer
        for (const auto &st : sts)
        {
            if (auto valueInst = llvm::dyn_cast<llvm::Instruction>(st->getPointerOperand()))
            {
                // walk fan-in to state store (data only)
                auto r = colors.insert(make_shared<NodeColor>(valueInst, OpColor::Blue));
                if (!r.second)
                {
                    (*r.first)->colors.insert(OpColor::Blue);
                }
                Q.push_front(valueInst);
                covered.insert(valueInst);
            }
        }*/
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
            memInstructions.insert(GetValueID(entry->inst));
        }
    }
    return memInstructions;
}

map<string, set<int64_t>> Cyclebite::Grammar::colorNodes( const map<string, set<llvm::BasicBlock *>>& kernelSets )
{
    map<string, set<int64_t>> categories;
    categories["KF"] = findFunction(kernelSets);
    categories["IV"] = findState(kernelSets);
    categories["BP"] = findMemory(kernelSets);
    // one characteristic about the categories is that each instruction may only belong to one category
    // the current graph coloring algorithms don't consider who is in which category at all, thus we do the exclusion algorithm here
    // Priority of categories:
    // 1. State
    // 2. Function
    // 3. Memory
    for( const auto& id : categories.at("IV") )
    {
        categories["KF"].erase(id);
        categories["BP"].erase(id);
    }
    for( const auto& f : categories.at("BP") )
    {
        categories["KF"].erase(f);
    }
    /*for( const auto& id : categories.at("KF") )
    {
        categories["BP"].erase(id);
    }*/
    return categories;
}