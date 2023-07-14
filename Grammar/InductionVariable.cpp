#include "InductionVariable.h"
#include "DataNode.h"
#include <deque>
#include <llvm/IR/Instructions.h>
#include "Util/Exceptions.h"
#include "Util/Print.h"
#include <spdlog/spdlog.h>

using namespace Cyclebite::Grammar;
using namespace Cyclebite::Graph;
using namespace std;

InductionVariable::InductionVariable( const std::shared_ptr<Cyclebite::Graph::DataNode>& n, const std::shared_ptr<Cycle>& c ) : Symbol("var"), cycle(c), node(n)
{
    // crawl the uses of the induction variable and try to ascertain what its dimensions are access patterns are
    deque<const llvm::Instruction*> Q;
    set<const llvm::Instruction*> covered;
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
    Q.push_front(n->getInst());
    covered.insert(n->getInst());
    // if the instruction we are given is a phi itself we need to add that to the phi set
    if( const auto phi = llvm::dyn_cast<llvm::PHINode>(node->getInst()) )
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
    // inspect all the binary operations done on the IV to ascertain what its stride pattern is
    // llvm ops defined at https://github.com/llvm-mirror/llvm/blob/master/include/llvm/IR/Instruction.def
    if( bins.size() != 1 )
    {
        // probably only one of these binary ops is used by the comparator
        // thus we walk backwards from the comparator and see who it uses
        if( cmps.size() != 1 )
        {
            PrintVal(node->getInst());
            throw AtlasException("Could not find a comparator for an IV!");
        }
        else if( cmps.size() > 1 )
        {
            PrintVal(node->getInst());
            throw AtlasException("Cannot yet handle an IV with multiple comparators!");
        }
        // now we need to find out how the IV is incremented
        // there generally are two cases on how an IV will be incremented
        // 1. (optimized case) the IV will bounce between a PHI and a binary op, and be used by comparators to determine the next state
        // 2. (unoptimized case) the IV will live in memory, thus its pointer will be used in store instructions, whose value operand is where the binary op can be found
        // thus we push the comparators and the stores into the queue and see what kind of binary ops we find
        Q.clear();
        covered.clear();
        Q.push_front(cycle->getIteratorInst());
        covered.insert(cycle->getIteratorInst());
        for ( const auto& st : sts )
        {
            Q.push_front(st);
            covered.insert(st);
        }
        while( !Q.empty() )
        {
            for( unsigned i = 0; i < Q.front()->getNumOperands(); i++ )
            {
                if( const auto useInst = llvm::dyn_cast<llvm::Instruction>(Q.front()->getOperand(i)) )
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
            PrintVal(node->getInst());
            throw AtlasException("Cannot yet handle an induction variable that is operated on by none or multiple operator!");
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
            throw AtlasException("Cannot yet handle opcode "+to_string(bin->getOpcode())+" that offsets an IV!");
    }
    // find out how the IV is initialized through its stores or phis
    int initValue = -1;
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
            throw AtlasException("Cannot handle an IV that is touched by more than one phi!");
        }
        // the phi should have two cases, one where the IV gets a value and one where the IV gets a constant
        // we want the constant case (the init value)
        auto phi = *phis.begin();
        for( unsigned i = 0; i < phi->getNumIncomingValues(); i++ )
        {
            if( auto con = llvm::dyn_cast<llvm::Constant>(phi->getIncomingValue(i)) )
            {
                initValue = (int)*con->getUniqueInteger().getRawData();
            }
        }
    }
    if( initValue < 0 )
    {
        PrintVal(node->getInst());
        throw AtlasException("Could not find initialization value for IV!");
    }
    // use the predicate from the comparator, the constant int boundary from the comparator, and the initial IV value to find the boundaries of the IV
    if( cmps.size() != 1 )
    {
        PrintVal(node->getInst());
        throw AtlasException("Cannot yet handle an induction variable that is compared more than once!");
    }
    auto cmp = *cmps.begin();
    // quick check here to make sure the sign we extract from the comparator makes sense
    // llvm::cmpinst* compare op0 to op1, that is, if cmp->getPredicate is gte, it is asking if op0 >= op1
    // right now, we assume op0 is the IV and op1 is thet condition boundary, thus if this is not true we throw an error
    if( !llvm::isa<llvm::Constant>(cmp->getOperand(1)) )
    {
        PrintVal(cmp->getOperand(0));
        PrintVal(n->getInst());
        throw AtlasException("Induction variable is not in position 0 of the comparator!");
    }
    int cmpBoundary = -1;
    for( unsigned i = 0; i < cmp->getNumOperands(); i++ )
    {
        if( auto con = llvm::dyn_cast<llvm::Constant>(cmp->getOperand(i)) )
        {
            cmpBoundary = (int)*con->getUniqueInteger().getRawData();
        }
    }
    if( cmpBoundary == -1 )
    {
        spdlog::critical("Could not find a constant boundary for an induction variable!");
        throw AtlasException("Could not find a constant boundary for an induction variable!");
    }
    switch(cmp->getPredicate())
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
            space.min = (uint32_t)cmpBoundary+1;
            space.max = (uint32_t)initValue;
            break;
        case 35: // integer unsigned greater or equal
            space.min = (uint32_t)cmpBoundary;
            space.max = (uint32_t)initValue;
            break;
        case 36: // integer unsigned less than
            space.min = (uint32_t)initValue;
            space.max = (uint32_t)cmpBoundary-1;
            break;
        case 37: // integer unsigned less or equal
            space.min = (uint32_t)initValue;
            space.max = (uint32_t)cmpBoundary;
            break;
        case 38: // integer signed greater than
            space.min = (uint32_t)cmpBoundary+1;
            space.max = (uint32_t)initValue;
            break;
        case 39: // integer signed greater or equal
            space.min = (uint32_t)cmpBoundary;
            space.max = (uint32_t)initValue;
            break;
        case 40: // integer signed less than
            space.min = (uint32_t)initValue;
            space.max = (uint32_t)cmpBoundary-1;
            break;
        case 41: // integer signed less or equal
            space.min = (uint32_t)initValue;
            space.max = (uint32_t)cmpBoundary;
            break;
        default:
            throw AtlasException("Cannot handle an induction variable whose comparator opcode is "+to_string(cmp->getPredicate()));
    }
    // sanity check, does the polyhedral space make sense
/*    switch(cmp->getPredicate())
    {
        case 32: // integer equal
        case 33: // integer not equal
        case 34: // integer unsigned greater than
            if( (space.stride < 0) && (cmpBoundary >= initValue) )
            {
                spdlog::critical("Found IV boundaries that will not iterate!");
                throw AtlasException("Found IV boundaries that will not iterate!");
            }
            else if( (space.stride > 0) && (cmpBoundary <= initValue) )
            {
                spdlog::critical("Found IV boundaries that will not terminate!");
                throw AtlasException("Found IV boundaries that will not terminate!");
            }
            break;
        case 35: // integer unsigned greater or equal
            if( (space.stride < 0) && (cmpBoundary > initValue) )
            {
                spdlog::critical("Found IV boundaries that will not iterate!");
                throw AtlasException("Found IV boundaries that will not iterate!");
            }            
            else if( (space.stride > 0) && (cmpBoundary < initValue) )
            {
                spdlog::critical("Found IV boundaries that will not terminate!");
                throw AtlasException("Found IV boundaries that will not terminate!");
            }
            break;
        case 36: // integer unsigned less than
            if( (space.stride > 0) && (cmpBoundary <= initValue) )
            {
                spdlog::critical("Found IV boundaries that will not allow for iteration!");
                throw AtlasException("Found IV boundaries that will not allow for iteration!");
            }
            break;
        case 37: // integer unsigned less or equal
            if( (space.stride > 0) && (cmpBoundary < initValue) )
            {
                spdlog::critical("Found IV boundaries that will not allow for iteration!");
                throw AtlasException("Found IV boundaries that will not allow for iteration!");
            }
            break;
        case 38: // integer signed greater than
        case 39: // integer signed greater or equal
        case 40: // integer signed less than
        case 41: // integer signed less or equal
        default:
            spdlog::critical("Cannot handle an induction variable whose comparator opcode is "+to_string(cmp->getPredicate()));
            throw AtlasException("Cannot handle an induction variable whose comparator opcode is "+to_string(cmp->getPredicate()));
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

const shared_ptr<DataNode>& InductionVariable::getNode() const
{
    return node;
}

const shared_ptr<Cycle>& InductionVariable::getCycle() const
{
    return cycle;
}

StridePattern InductionVariable::getPattern() const
{
    return pat;
}

const set<shared_ptr<Cyclebite::Graph::ControlBlock>, Cyclebite::Graph::p_GNCompare>& InductionVariable::getBody() const
{
    return body;
}

const PolySpace InductionVariable::getSpace() const
{
    return space;
}

string InductionVariable::dump() const
{
    return name;
}

bool InductionVariable::isOffset(const llvm::Value* v) const
{
    if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(v) )
    {
        deque<const llvm::Value*> Q;
        set<const llvm::Value*> covered;
        Q.push_front(node->getInst());
        covered.insert(node->getInst());
        while( !Q.empty() )
        {
            if( Q.front() == v )
            {
                // this is the value we are looking for, return true
                return true;
            }
            if( Q.front() == node->getInst() )
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
            else if( const auto& bin = llvm::dyn_cast<llvm::BinaryOperator>(Q.front()) )
            {
                // binary ops may be offsetting the IV, thus its users can be searched through
                for( const auto& user : bin->users() )
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
                // loads indicate an access to the induction variable (most commonly found in the unoptimized case)
                for( const auto& user : ld->users() )
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
    }
    return false;
}