//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "InductionVariable.h"
#include "ReductionVariable.h"
#include "IO.h"
#include "Task.h"
#include "Graph/inc/IO.h"
#include "Transforms.h"
#include "Util/Annotate.h"
#include <deque>
#include <llvm/IR/Instructions.h>
#include "Util/Exceptions.h"
#include "Util/Print.h"
#include <spdlog/spdlog.h>

using namespace Cyclebite::Grammar;
using namespace Cyclebite::Graph;
using namespace std;

InductionVariable::InductionVariable( const std::shared_ptr<Cyclebite::Graph::DataValue>& n, const std::shared_ptr<Cycle>& c, const llvm::Instruction* targetExit ) : Counter(n, c), Symbol("var")
{
    // crawl the uses of the induction variable and try to ascertain what its dimensions and access patterns are
    deque<const llvm::Value*> Q;
    set<const llvm::Value*> covered;
    // binary operators tell us how the induction variable is incremented
    set<const llvm::BinaryOperator*> bins;
    // geps tell us which collections may be used by this IV
    set<const llvm::GetElementPtrInst*> geps;
    // comparators tell us what the boundaries of the IV are
    set<const llvm::CmpInst*> cmps;
    // stores tell us how the IV is initialized (in the case of unoptimized code)
    set<const llvm::StoreInst*> sts;
    // PHIs tell us how the IV is initialized (in the case of optimized code)
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
            // stay away from uses in the function group
            else if( Cyclebite::Graph::DNIDMap.contains(user) )
            {
                if( const auto& inst = dynamic_pointer_cast<Cyclebite::Graph::Inst>( Cyclebite::Graph::DNIDMap.at(user) ) )
                {
                    if( !inst->isFunction() )
                    {
                        if( covered.find(inst->getInst()) == covered.end() )
                        {
                            Q.push_back(inst->getInst());
                            covered.insert(inst->getInst());
                        }
                    }
                }
            }
        }
        Q.pop_front();
    }
    // inspect all the binary operations done on the IV to ascertain what its stride pattern is
    // llvm ops defined at https://github.com/llvm-mirror/llvm/blob/master/include/llvm/IR/Instruction.def
    if( bins.size() != 1 )
    {
        // there generally are two cases on how an IV will be incremented
        // 1. (optimized case) the IV will bounce between a PHI and a binary op, and be used by comparators to determine the next state
        // 2. (unoptimized case) the IV will live in memory, thus its pointer will be used in store instructions, whose value operand is where the binary op can be found
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
                if( static_pointer_cast<Inst>(DNIDMap.at(Q.front()))->isState() )
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
    int stride = static_cast<int>(STATIC_VALUE::INVALID);
    switch(bin->getOpcode())
    {
        case 13: // add
            for( unsigned i = 0; i < bin->getNumOperands(); i++ )
            {
                if( auto con = llvm::dyn_cast<llvm::Constant>(bin->getOperand(i)) )
                {
                    stride = (int)*con->getUniqueInteger().getRawData();
                    break;
                }
                else
                {
                    stride = static_cast<int>(STATIC_VALUE::UNDETERMINED);
                }
            }
            break;
        case 26: // shift right
            for( unsigned i = 0; i < bin->getNumOperands(); i++ )
            {
                if( const auto& con = llvm::dyn_cast<llvm::Constant>(bin->getOperand(i)) )
                {
                    auto conInt = (int)*con->getUniqueInteger().getRawData();
                    // we shift to the right by this much in each iteration... which is not consistant
                    // this is basically a divide-by-constant at each iteration
                    // for now we just make the stride the divide-by factor
                    stride = conInt;
                    break;
                }
                else
                {
                    stride = static_cast<int>(STATIC_VALUE::UNDETERMINED);
                }
            }
            break;
        default:
            throw CyclebiteException("Cannot yet handle opcode "+string(Cyclebite::Graph::OperationToString.at(Cyclebite::Graph::GetOp(bin->getOpcode())))+" (opcode "+to_string(bin->getOpcode())+") that offsets an IV!");
    }
    // find out how the IV is initialized through its stores or phis
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
        // we want the phi that is directly used in the target exit
        const llvm::PHINode* targetPhi = nullptr;
        if( phis.size() == 1 )
        {
            targetPhi = *phis.begin();
        }
        else
        {
            for( const auto& phi : phis )
            {
                deque<const llvm::Value*> instQ;
                set<const llvm::Value*> instCovered;
                if( const auto& br = llvm::dyn_cast<llvm::BranchInst>(targetExit) )
                {
                    instQ.push_front(br->getCondition());
                    instCovered.insert(br->getCondition());
                }
                else
                {
                    PrintVal(targetExit);
                    throw CyclebiteException("Cannot yet handle this targetExit type when searching for initial IV value!");
                }
                while( !instQ.empty() )
                {
                    if( instQ.front() == phi )
                    {
                        targetPhi = phi;
                        break;
                    }
                    if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(instQ.front()) )
                    {
                        for( const auto& op : inst->operands() )
                        {
                            if( !instCovered.contains(op) )
                            {
                                instQ.push_back(op);
                                instCovered.insert(op);
                            }
                        }
                    }
                    instQ.pop_front();
                }
                if( targetPhi )
                {
                    break;
                }
            }
        }
        // the phi should have two cases, one where the IV gets a value and one where the IV gets a constant
        // we want the constant case (the init value)
        for( unsigned i = 0; i < targetPhi->getNumIncomingValues(); i++ )
        {
            // we need to figure out if this is the initialization value of the phi
            // this can be found out in two ways
            // first, the incoming value is a constant - this *most likely* points to the initialization value
            // second, the incoming value comes from a block that is not this one (most of the time, when the optimizer is on, the incoming value from a block outside the current is the init)
            // third, throw an error because the space becomes more complicated and we haven't had a reason to solve this problem yet
            if( auto con = llvm::dyn_cast<llvm::Constant>(targetPhi->getIncomingValue(i)) )
            {
                initValue = (int)*con->getUniqueInteger().getRawData();
                break;
            }
            else if( targetPhi->getIncomingBlock(i) != targetPhi->getParent() )
            {
                // the init value is not a constant, we use the dynamically observed information to find out what the frequency of this block was
                // the frequency of the block *probably* tells us what the frequency of this task was
                // this breaks down when the task was called repeatedly 
                //  - for now, the belief is that this doesn't matter. EP ensures the task we are evaluating is a good accelerator candidate, and we trust EP
                // TODO: the IO.cpp/BuildDFG() method doesn't yet populate the edges between ControlBlocks, so this will always return 0
                //initValue = (int)BBCBMap.at(targetPhi->getParent())->getFrequency();
                initValue = static_cast<int>(STATIC_VALUE::UNDETERMINED);
            }
        }
    }
    else
    {
        PrintVal(n->getVal());
        PrintVal(targetExit);
        throw CyclebiteException("Could not find a starting place to determine the initial value of an IV!");
    }
    if( initValue == static_cast<int>(STATIC_VALUE::INVALID) )
    {
        PrintVal(node->getVal());
        throw CyclebiteException("Could not find initialization value for IV!");
    }
    // quick check here to make sure the sign we extract from the comparator makes sense
    // llvm::cmpinst* compare op0 to op1, that is, if cmp->getPredicate is gte, it is asking if op0 >= op1
    // right now, we assume op0 is the IV and op1 is the condition boundary, thus if this is not true we throw an error
    const llvm::CmpInst* targetCmp = nullptr;
    if( const auto& br = llvm::dyn_cast<llvm::BranchInst>(targetExit) )
    {
        if( const auto& tc = llvm::dyn_cast<llvm::CmpInst>(br->getCondition()) )
        {
            targetCmp = tc;
        }
        else if( const auto& sel = llvm::dyn_cast<llvm::SelectInst>(br->getCondition()) )
        {
            // the select has two incoming values, selected as the output by the select condition
            // the avenue that leads to the phi node is the one we want
            // if both avenues lead us there, pick an arbitrary one
            set<llvm::Instruction*> toTest;
            for( const auto& op : sel->operands() )
            {
                if( const auto& opInst = llvm::dyn_cast<llvm::Instruction>(op) )
                {
                    toTest.insert(opInst);
                }
            }
            for( const auto& test : toTest )
            {
                deque<const llvm::Instruction*> Q;
                set<const llvm::Instruction*> covered;
                const llvm::CmpInst* btwCmp = nullptr;
                bool found = false;
                Q.push_front(test);
                covered.insert(test);
                while( !Q.empty() )
                {
                    if( node->getVal() == Q.front() )
                    {
                        // we have found the IV, the search is over
                        found = true;
                        break;
                    }
                    else if( const auto& cmp = llvm::dyn_cast<llvm::CmpInst>(Q.front()) )
                    {
                        btwCmp = cmp;
                    }
                    for( const auto& op : Q.front()->operands() )
                    {
                        if( const auto& opInst = llvm::dyn_cast<llvm::Instruction>(op) )
                        {
                            if( !covered.contains(opInst) )
                            {
                                Q.push_back(opInst);
                                covered.insert(opInst);
                            }
                        }
                    }
                    Q.pop_front();
                }
                if( found && btwCmp )
                {
                    targetCmp = btwCmp;
                    break;
                }
            }
            if( !targetCmp )
            {
                throw CyclebiteException("Cannot resolve comparator through select instruction paths!");
            }
        }
        else
        {
#ifdef DEBUG
            PrintVal(node->getVal());
            PrintVal(br->getCondition());
            PrintVal(br);
#endif
            throw CyclebiteException("Cycle iterator inst was not fed by a recognized instruction type!");
        }
    }
    else
    {
        throw CyclebiteException("Cannot yet support non-branch cycle exits!");
    }
    // here we figure out what the means for the induction variable
    // for example, if the IV is in position 0 of the comparator, then the comparator's operation does not need to be inverted (e.g., IV < thresh means the lt can be taken literally)
    // if the IV is in position 1 of the IV, the operation needs to be inverted (e.g., thresh < IV means the lt actually needs to be gt)
    int cmpBoundary = static_cast<int>(STATIC_VALUE::INVALID);
    for( unsigned i = 0; i < targetCmp->getNumOperands(); i++ )
    {
        if( auto con = llvm::dyn_cast<llvm::Constant>(targetCmp->getOperand(i)) )
        {
            cmpBoundary = (int)*con->getUniqueInteger().getRawData();
            break;
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
    if( initValue != static_cast<int>(STATIC_VALUE::UNDETERMINED) && (cmpBoundary != static_cast<int>(STATIC_VALUE::UNDETERMINED)) && (stride != static_cast<int>(STATIC_VALUE::UNDETERMINED)) )
    {
        space.min = initValue < cmpBoundary ? initValue : cmpBoundary;
        space.max = space.min == initValue ? cmpBoundary : initValue;
        space.stride = stride;
        space.pattern = StridePattern::Sequential;
    }
    else if( initValue != static_cast<int>(STATIC_VALUE::UNDETERMINED) )
    {
        // the cmpBoundary is undetermined
        // the stride sign determines whether min/max gets initValue
        if( stride == static_cast<int>(STATIC_VALUE::UNDETERMINED) )
        {
            // we just arbitrarily assign initValue to min
            space.min = initValue;
            space.max = static_cast<int>(STATIC_VALUE::UNDETERMINED);
            space.stride = stride;
            space.pattern = StridePattern::Random;
        }
        else if( stride < 0 )
        {
            space.min = static_cast<int>(STATIC_VALUE::UNDETERMINED);
            space.max = initValue;
            space.stride = stride;
            space.pattern = StridePattern::Sequential;
        }
        else 
        {
            space.min = initValue;
            space.max = static_cast<int>(STATIC_VALUE::UNDETERMINED);
            space.stride = stride;
            space.pattern = StridePattern::Sequential;
        }
    }
    else if( cmpBoundary != static_cast<int>(STATIC_VALUE::UNDETERMINED) )
    {
        // the initValue is undetermined
        // the stride sign determines whether min/max gets cmpBoundary
        if( stride == static_cast<int>(STATIC_VALUE::UNDETERMINED) )
        {
            // we just arbitrarily assign cmpBoundary to max
            space.min = static_cast<int>(STATIC_VALUE::UNDETERMINED);
            space.max = cmpBoundary;
            space.stride = stride;
            space.pattern = StridePattern::Random;
        }
        else if( stride < 0 )
        {
            space.min = cmpBoundary;
            space.max = static_cast<int>(STATIC_VALUE::UNDETERMINED);
            space.stride = stride;
            space.pattern = StridePattern::Sequential;
        }
        else 
        {
            space.min = static_cast<int>(STATIC_VALUE::UNDETERMINED);
            space.max = cmpBoundary;
            space.stride = stride;
            space.pattern = StridePattern::Sequential;
        }
    }

    /*
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
    // sanity check, does the polyhedral space make sense
/*    switch(targetCmp->getPredicate())
    {
        case 32: // integer equal
        case 33: // integer not equal
        case 34: // integer unsigned greater than
            if( (space.stride < 0) && (cmpBoundary >= initValue) )
            {
                spdlog::critical("Found IV boundaries that will not iterate!");
                throw CyclebiteException("Found IV boundaries that will not iterate!");
            }
            else if( (space.stride > 0) && (cmpBoundary <= initValue) )
            {
                spdlog::critical("Found IV boundaries that will not terminate!");
                throw CyclebiteException("Found IV boundaries that will not terminate!");
            }
            break;
        case 35: // integer unsigned greater or equal
            if( (space.stride < 0) && (cmpBoundary > initValue) )
            {
                spdlog::critical("Found IV boundaries that will not iterate!");
                throw CyclebiteException("Found IV boundaries that will not iterate!");
            }            
            else if( (space.stride > 0) && (cmpBoundary < initValue) )
            {
                spdlog::critical("Found IV boundaries that will not terminate!");
                throw CyclebiteException("Found IV boundaries that will not terminate!");
            }
            break;
        case 36: // integer unsigned less than
            if( (space.stride > 0) && (cmpBoundary <= initValue) )
            {
                spdlog::critical("Found IV boundaries that will not allow for iteration!");
                throw CyclebiteException("Found IV boundaries that will not allow for iteration!");
            }
            break;
        case 37: // integer unsigned less or equal
            if( (space.stride > 0) && (cmpBoundary < initValue) )
            {
                spdlog::critical("Found IV boundaries that will not allow for iteration!");
                throw CyclebiteException("Found IV boundaries that will not allow for iteration!");
            }
            break;
        case 38: // integer signed greater than
        case 39: // integer signed greater or equal
        case 40: // integer signed less than
        case 41: // integer signed less or equal
        default:
            spdlog::critical("Cannot handle an induction variable whose comparator opcode is "+to_string(targetCmp->getPredicate()));
            throw CyclebiteException("Cannot handle an induction variable whose comparator opcode is "+to_string(targetCmp->getPredicate()));
    }*/

    // build out loop body
    // this will break if the induction variable is used for multiple loops
    /*for( const auto& cmp : cmps )
    {
        if( cmp->getNumSuccessors() > 1 )
        {
            // we have found a comparator that uses our IV and has multiple destinations, likely indicating a loop condition
            // now we have to find which successor means continuing to loop
            for( unsigned i = 0; i < cmp->getNumSuccessors(); i++ )
            {
                auto succ = cmp->getSuccessor(i);

            }
        }
    }*/
}

string InductionVariable::dump() const
{
    return name;
}

string InductionVariable::dumpHalide( const map<shared_ptr<Dimension>, shared_ptr<ReductionVariable>>& dimToRV ) const
{
    for( const auto& dim : dimToRV )
    {
        if( dim.first.get() == this )
        {
            return dim.second->getName();
        }
    }
    return name;
}

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
        // non-child exits will capture both hierarchical loops and cycle exits
        for( const auto& e : cycle->getNonChildExits() )
        {
            auto d = static_pointer_cast<Inst>(DNIDMap.at(e));
            if( (d->isTerminator()) && (d->parent->getSuccessors().size() > 1) )
            {
                // we have a multi-destination control instruction, walk its predecessors to find a memory or binary operation that indicates an induction variable
                set<shared_ptr<DataValue>, p_GNCompare> vars;
                set<const llvm::Instruction*> covered;
                deque<const llvm::Instruction*> Q;
                Q.push_front(llvm::cast<llvm::Instruction>(d->getVal()));
                covered.insert(llvm::cast<llvm::Instruction>(d->getVal()));
                PrintVal(Q.front());
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
                            // any phi within the current cycle that is used by a branch iterator inst is an IV candidate
                            // later we see which phis have a binary instruction user within the given cycle, these phis will become IVs and filter those that come from elsewhere or set dynamic boundaries
                            covered.insert(bin);
                            covered.insert(phi);
                            vars.insert( Cyclebite::Graph::DNIDMap.at((llvm::Instruction*)phi) );
                        }
                        else if( auto ld = llvm::dyn_cast<llvm::LoadInst>(use.get()) )
                        {
                            // case found in unoptimized programs when the induction variable lives on the heap (not in a value) and is communicated with through ld/st
                            // the pointer argument to this load is likely the induction variable pointer, so add that to the vars set
                            covered.insert(ld);
                            if( const auto& p_inst = llvm::dyn_cast<llvm::Instruction>(ld->getPointerOperand()) )
                            {
                                // make sure this IV is alive
                                if( BBCBMap.find(p_inst->getParent()) != BBCBMap.end() )
                                {
                                    vars.insert( Cyclebite::Graph::DNIDMap.at((llvm::Instruction*)ld->getPointerOperand()) );
                                }
                            }
                        }
                        else if( const auto& sel = llvm::dyn_cast<llvm::SelectInst>(use.get()) )
                        {
                            for( const auto& op : sel->operands() )
                            {
                                if( const auto& opInst = llvm::dyn_cast<llvm::Instruction>(op) )
                                {
                                    if( !covered.contains(opInst) )
                                    {
                                        Q.push_back(opInst);
                                        covered.insert(opInst);
                                    }
                                }
                            }
                        }
                    }
                    Q.pop_front();
                }
                if( vars.empty() )
                {
                    throw CyclebiteException("Could not find any IVs for this cycle!");
                }
                for( const auto& var : vars )
                {
                    // make sure it has a binary operation within the cycle itself
                    // this will distinguish true IVs from dynamic boundaries that may be loaded and stored to just like IVs
                    // in order to be a candidate, the var must be manipulated by an llvm::BinaryOperator within the task itself
                    // this differentiates the IV from a dynamic boundary that is captured elsewhere
                    bool foundBin = false;
                    Q.clear();
                    covered.clear();
                    Q.push_front(static_pointer_cast<Inst>(var)->getInst());
                    covered.insert(static_pointer_cast<Inst>(var)->getInst());
                    while( !Q.empty() )
                    {
                        for( const auto& use : Q.front()->users() )
                        {
                            if( DNIDMap.find(use) == DNIDMap.end() )
                            {
                                continue;
                            }
                            if( const auto& bin = llvm::dyn_cast<llvm::BinaryOperator>(use) )
                            {
                                if( cycle->find(DNIDMap.at(bin)) )
                                {
                                    foundBin = true;
                                    break;
                                }
                            }
                            else if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(use) )
                            {
                                if( cycle->find(DNIDMap.at(inst)) )
                                {
                                    if( covered.find(inst) == covered.end() )
                                    {
                                        Q.push_back(inst);
                                        covered.insert(inst);
                                    }
                                }
                            }
                        }
                        if( foundBin )
                        {
                            auto newIV = make_shared<InductionVariable>(var, cycle, e);
                            IVs.insert(newIV);
                            Q.clear();
                            break;
                        }
                        Q.pop_front();
                    }
                }
            }
        }
    }
    return IVs;
}