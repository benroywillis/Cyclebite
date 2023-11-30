//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Counter.h"
#include "Util/Exceptions.h"
#include "Util/Print.h"
#include "Graph/inc/IO.h"
#include "Graph/inc/Inst.h"
#include <deque>
#include <llvm/IR/Instructions.h>

using namespace std;
using namespace Cyclebite::Grammar;

Counter::Counter( const shared_ptr<Cyclebite::Graph::DataValue>& n, const shared_ptr<Cycle>& c ) : Dimension(n, c)
{
    /*// crawl the uses of the induction variable and try to ascertain what its dimensions and access patterns are
    deque<const llvm::Value*> Q;
    set<const llvm::Value*> covered;
    // binary operators tell us how the induction variable is incremented
    set<const llvm::BinaryOperator*> bins;
    // geps tell us which collections may be used by this counter
    set<const llvm::GetElementPtrInst*> geps;
    // comparators tell us what the boundaries of the counter are
    set<const llvm::CmpInst*> cmps;
    // stores tell us how the counter is initialized (in the case of unoptimized code)
    set<const llvm::StoreInst*> sts;
    // PHIs tell us how the counter is initialized (in the case of optimized code)
    set<const llvm::PHINode*> phis;
    Q.push_front(n->getVal());
    covered.insert(n->getVal());
    // if the instruction we are given is a phi itself we need to add that to the phi set
    if( const auto phi = llvm::dyn_cast<llvm::PHINode>(node->getVal()) )
    {
        phis.insert(phi);
    }
    while( !Q.empty() )
    {
        for( const auto user : Q.front()->users() )
        {
            if( auto bin = llvm::dyn_cast<llvm::BinaryOperator>(user) )
            {
                // binary users may lead to a comparator
                if( covered.find(bin) == covered.end() )
                {
                    bins.insert(bin);
                    covered.insert(bin);
                    Q.push_back(bin);
                }
            }
            else if( auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(user) )
            {
                geps.insert(gep);
            }
            else if( auto cmp = llvm::dyn_cast<llvm::CmpInst>(user) )
            {
                cmps.insert(cmp);
            }
            else if( auto st = llvm::dyn_cast<llvm::StoreInst>(user) )
            {
                sts.insert(st);
            }
            else if( auto phi = llvm::dyn_cast<llvm::PHINode>(user) )
            {
                phis.insert(phi);
            }
            else if( auto inst = llvm::dyn_cast<llvm::Instruction>(user) )
            {
                if( covered.find(inst) == covered.end() )
                {
                    Q.push_back(inst);
                    covered.insert(inst);
                }
            }
        }
        Q.pop_front();
    }
    // inspect all the binary operations done on the counter to ascertain what its stride pattern is
    // llvm ops defined at https://github.com/llvm-mirror/llvm/blob/master/include/llvm/IR/Instruction.def
    if( bins.size() != 1 )
    {
        // there generally are two cases on how an counter will be incremented
        // 1. (optimized case) the counter will bounce between a PHI and a binary op, and be used by comparators to determine the next state
        // 2. (unoptimized case) the counter will live in memory, thus its pointer will be used in store instructions, whose value operand is where the binary op can be found
        // thus we push the comparators and the stores into the queue and see what kind of binary ops we find
        Q.clear();
        covered.clear();
        Q.push_front(targetExit);
        covered.insert(targetExit);
        for ( const auto& st : sts )
        {
            Q.push_front(st);
            covered.insert(st);
        }
        while( !Q.empty() )
        {
            if( auto inst = llvm::dyn_cast<llvm::Instruction>(Q.front()) )
            {
                // we are only interested in finding the instructions that control the algorithm, the other binary ops are used for other things (likely memory space, but possibly function too)
                // thus, if you are not in the control group, we don't pay attention to you
                if( static_pointer_cast<Graph::Inst>(Graph::DNIDMap.at(Q.front()))->isState() )
                {
                    for( unsigned i = 0; i < inst->getNumOperands(); i++ )
                    {
                        if( const auto useInst = llvm::dyn_cast<llvm::Instruction>(inst->getOperand(i)) )
                        {
                            if( auto bin = llvm::dyn_cast<llvm::BinaryOperator>(useInst) )
                            {
                                if( bins.find(bin) != bins.end() )
                                {
                                    covered.insert(bin);
                                    Q.push_back(bin);
                                }
                            }
                            else if( covered.find(useInst) == covered.end() )
                            {
                                covered.insert(useInst);
                                Q.push_back(useInst);
                            }
                        }
                    }
                }
            }
            if( Q.empty() )
            {
                break;
            }
            else
            {
                Q.pop_front();
            }
        }
        // the bin that is in the covered set is the one we want to keep
        auto binCpy = bins;
        for( const auto& bb : binCpy )
        {
            if( covered.find(bb) == covered.end() )
            {
                bins.erase(bb);
            }
        }
        if( bins.size() != 1 )
        {
            PrintVal(node->getVal());
            throw CyclebiteException("Cannot yet handle an induction variable that is operated on by none or multiple operators!");
        }
    }
    auto bin = *bins.begin();
    switch(bin->getOpcode())
    {
        case 13: // add
            for( unsigned i = 0; i < bin->getNumOperands(); i++ )
            {
                if( auto con = llvm::dyn_cast<llvm::Constant>(bin->getOperand(i)) )
                {
                    space.stride = (uint32_t)*con->getUniqueInteger().getRawData();
                }
            }
            break;
        default:
            throw CyclebiteException("Cannot yet handle opcode "+to_string(bin->getOpcode())+" that offsets a counter!");
    }
    // find out how the counter is initialized through its stores or phis
    int initValue = static_cast<int>(STATIC_VALUE::INVALID);
    if( !sts.empty() )
    {
        // likely the unoptimized case 
        // there should be two stores: an initial store for the init (with a constant) and another store for the increment
        // we are looking for the init store, which should have a constant
        for( const auto& st : sts )
        {
            if( auto con = llvm::dyn_cast<llvm::Constant>(st->getValueOperand()) )
            {
                initValue = (int)*con->getUniqueInteger().getRawData();
            }
        }
    }
    else if( !phis.empty() )
    {
        // likely the optimized case
        // we can only handle the case with one phi
        if( phis.size() != 1 )
        {
            for( const auto& phi : phis )
            {
                PrintVal(phi);
            }
            throw CyclebiteException("Cannot handle a counter that is touched by more than one phi!");
        }
        // the phi should have two cases, one where the counter gets a value and one where the counter gets a constant
        // we want the constant case (the init value)
        auto phi = *phis.begin();
        for( unsigned i = 0; i < phi->getNumIncomingValues(); i++ )
        {
            // we need to figure out if this is the initialization value of the phi
            // this can be found out in two ways
            // first, the incoming value is a constant - this *most likely* points to the initialization value
            // second, the incoming value comes from a block that is not this one (most of the time, when the optimizer is on, the incoming value from a block outside the current is the init)
            // third, throw an error because the space becomes more complicated and we haven't had a reason to solve this problem yet
            if( auto con = llvm::dyn_cast<llvm::Constant>(phi->getIncomingValue(i)) )
            {
                initValue = (int)*con->getUniqueInteger().getRawData();
            }
            else if( phi->getIncomingBlock(i) != phi->getParent() )
            {
                // the init value is not a constant, we use the dynamically observed information to find out what the frequency of this block was
                // the frequency of the block *probably* tells us what the frequency of this task was
                // this breaks down when the task was called repeatedly 
                //  - for now, the belief is that this doesn't matter. EP ensures the task we are evaluating is a good accelerator candidate, and we trust EP
                // TODO: the IO.cpp/BuildDFG() method doesn't yet populate the edges between ControlBlocks, so this will always return 0
                //initValue = (int)BBCBMap.at(phi->getParent())->getFrequency();
                initValue = static_cast<int>(STATIC_VALUE::UNDETERMINED);
            }
        }
    }
    if( initValue == static_cast<int>(STATIC_VALUE::INVALID) )
    {
        PrintVal(node->getVal());
        throw CyclebiteException("Could not find initialization value for counter!");
    }
    // quick check here to make sure the sign we extract from the comparator makes sense
    // llvm::cmpinst* compare op0 to op1, that is, if cmp->getPredicate is gte, it is asking if op0 >= op1
    // right now, we assume op0 is the counter and op1 is the condition boundary, thus if this is not true we throw an error
    llvm::CmpInst* targetCmp = nullptr;
    if( const auto& br = llvm::dyn_cast<llvm::BranchInst>(targetExit) )
    {
        targetCmp = llvm::dyn_cast<llvm::CmpInst>(br->getCondition());
        if( !targetCmp )
        {
#ifdef DEBUG
            PrintVal(node->getVal());
            PrintVal(br->getCondition());
            PrintVal(br);
#endif
            throw CyclebiteException("Cycle iterator inst was not fed by a condition!");
        }
    }
    else
    {
        throw CyclebiteException("Cannot yet support non-branch cycle exits!");
    }
    // here we figure out what the means for the induction variable
    // for example, if the counter is in position 0 of the comparator, then the comparator's operation does not need to be inverted (e.g., counter < thresh means the lt can be taken literally)
    // if the counter is in position 1 of the PHINode, the operation needs to be inverted (e.g., thresh < counter means the lt actually needs to be gt)
    bool invert = false;
    if( targetCmp->getOperand(1) == node->getVal() )
    {
        invert = true;
    }
    int cmpBoundary = static_cast<int>(STATIC_VALUE::INVALID);
    for( unsigned i = 0; i < targetCmp->getNumOperands(); i++ )
    {
        if( auto con = llvm::dyn_cast<llvm::Constant>(targetCmp->getOperand(i)) )
        {
            cmpBoundary = (int)*con->getUniqueInteger().getRawData();
        }
        else
        {
            // TODO: walk backward through the dataflow to find the value, if it is determinable
            cmpBoundary = static_cast<int>(STATIC_VALUE::UNDETERMINED); 
        }
    }
    if( cmpBoundary == static_cast<int>(STATIC_VALUE::INVALID) )
    {
#ifdef DEBUG
        PrintVal(node->getVal());
        PrintVal(targetCmp);
#endif
        throw CyclebiteException("Could not find a valid boundary for an induction variable!");
    }
    switch(targetCmp->getPredicate())
    {
        case 32: // integer equal
            space.min = (uint32_t)cmpBoundary;
            space.max = (uint32_t)cmpBoundary;
            break;
        case 33: // integer not equal
            if( cmpBoundary >= initValue )
            {
                space.min = (uint32_t)initValue;
                space.max = (uint32_t)cmpBoundary;
            }
            else
            {
                space.min = (uint32_t)cmpBoundary;
                space.max = (uint32_t)initValue;
            }
            break;
        case 34: // integer unsigned greater than
            if( invert )
            {
                space.min = (uint32_t)initValue;
                space.max = (uint32_t)cmpBoundary+1;
            }
            else
            {
                space.min = (uint32_t)cmpBoundary+1;
                space.max = (uint32_t)initValue;
            }
            break;
        case 35: // integer unsigned greater or equal
            if( invert )
            {
                space.min = (uint32_t)initValue;
                space.max = (uint32_t)cmpBoundary;
            }
            else
            {
                space.min = (uint32_t)cmpBoundary;
                space.max = (uint32_t)initValue;
            }
            break;
        case 36: // integer unsigned less than
            if( invert )
            {
                space.min = (uint32_t)cmpBoundary-1;
                space.max = (uint32_t)initValue;
            }
            else
            {
                space.min = (uint32_t)initValue;
                space.max = (uint32_t)cmpBoundary-1;
            }
            break;
        case 37: // integer unsigned less or equal
            if( invert )
            {
                space.max = (uint32_t)initValue;
                space.min = (uint32_t)cmpBoundary;
            }
            else
            {
                space.min = (uint32_t)initValue;
                space.max = (uint32_t)cmpBoundary;
            }
            break;
        case 38: // integer signed greater than
            if( invert )
            {
                space.max = (uint32_t)initValue;
                space.min = (uint32_t)cmpBoundary;
            }
            else
            {
                space.min = (uint32_t)cmpBoundary+1;
                space.max = (uint32_t)initValue;
            }
            break;
        case 39: // integer signed greater or equal
            if( invert )
            {
                space.max = (uint32_t)initValue;
                space.min = (uint32_t)cmpBoundary;
            }
            else
            {
                space.min = (uint32_t)cmpBoundary;
                space.max = (uint32_t)initValue;
            }
            break;
        case 40: // integer signed less than
            if( invert )
            {
                space.max = (uint32_t)initValue;
                space.min = (uint32_t)cmpBoundary;
            }
            else
            {
                space.min = (uint32_t)initValue;
                space.max = (uint32_t)cmpBoundary-1;
            }
            break;
        case 41: // integer signed less or equal
            if( invert )
            {
                space.max = (uint32_t)initValue;
                space.min = (uint32_t)cmpBoundary;
            }
            else
            {
                space.min = (uint32_t)initValue;
                space.max = (uint32_t)cmpBoundary;
            }
            break;
        default:
            throw CyclebiteException("Cannot handle an induction variable whose comparator opcode is "+to_string(targetCmp->getPredicate()));
    }*/
}


StridePattern Counter::getPattern() const
{
    return pat;
}

const PolySpace Counter::getSpace() const
{
    return space;
}

/*set<shared_ptr<Counter>> getCounters(const std::shared_ptr<Task>& t)
{
    set<shared_ptr<Counter>> counts;


    return counts;
}*/
