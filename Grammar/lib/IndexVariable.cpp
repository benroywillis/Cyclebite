//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "IndexVariable.h"
#include <deque>
#include "Task.h"
#include "IO.h"
#include "Graph/inc/IO.h"
#include "Transforms.h"
#include "Inst.h"
#include "Util/Annotate.h"
#include "BasePointer.h"
#include "InductionVariable.h"
#include <llvm/IR/Instructions.h>
#include "Util/Exceptions.h"
#include "Util/Print.h"
#include <spdlog/spdlog.h>

using namespace std;
using namespace Cyclebite::Grammar;

IndexVariable::IndexVariable( const std::shared_ptr<Cyclebite::Graph::Inst>& n, 
                              const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& p, 
                              const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& c,
                              bool il ) : Symbol("idx"), node(n), parent(p), children(c), IL(il) {}

void IndexVariable::addChild( const shared_ptr<IndexVariable>& c )
{
    children.insert(c);
}

void IndexVariable::setParent( const shared_ptr<IndexVariable>& p)
{
    parent = p;
}

void IndexVariable::setIV( const shared_ptr<InductionVariable>& indVar )
{
    iv = indVar;
}

void IndexVariable::addBP( const shared_ptr<BasePointer>& baseP )
{
    bps.insert(baseP);
}

void IndexVariable::setLoaded( bool loaded )
{
    IL = loaded;
}

const shared_ptr<Cyclebite::Graph::Inst>& IndexVariable::getNode() const
{
    return node;
}

const set<shared_ptr<Cyclebite::Graph::Inst>, Cyclebite::Graph::p_GNCompare> IndexVariable::getGeps() const
{
    set<shared_ptr<Graph::Inst>, Graph::p_GNCompare> geps;
    deque<shared_ptr<Graph::Inst>> Q;
    set<shared_ptr<Graph::Inst>, Graph::p_GNCompare> covered;
    Q.push_front(node);
    covered.insert(node);
    while( !Q.empty() )
    {
        if( Q.front()->getOp() == Graph::Operation::gep )
        {
            geps.insert(Q.front());
        }
        else
        {
            for( const auto& user : Q.front()->getSuccessors() )
            {
                if( const auto& inst = dynamic_pointer_cast<Graph::Inst>(user->getSnk()) )
                {
                    if( covered.find(inst) == covered.end() )
                    {
                        Q.push_back(inst);
                        covered.insert(inst);
                    }
                }
            }
        }
        Q.pop_front();
    }
    return geps;
}

const shared_ptr<Cyclebite::Grammar::IndexVariable>& IndexVariable::getParent() const
{
    return parent;
}

const set<shared_ptr<Cyclebite::Grammar::IndexVariable>>& IndexVariable::getChildren() const
{
    return children;
}

const shared_ptr<InductionVariable>& IndexVariable::getIV() const
{
    return iv;
}

const set<shared_ptr<BasePointer>>& IndexVariable::getBPs() const
{
    return bps;
}

string IndexVariable::dump() const
{
    return name;
}

const PolySpace IndexVariable::getSpace() const
{
    return space;
}

bool IndexVariable::isLoaded() const
{
    return IL;
}

set<shared_ptr<IndexVariable>> Cyclebite::Grammar::getIndexVariables(const shared_ptr<Task>& t, const set<shared_ptr<BasePointer>>& BPs, const set<shared_ptr<InductionVariable>>& vars)
{
    // final set of index variables that may be found
    set<shared_ptr<IndexVariable>> idxVars;

    // mapping between base pointers and their index variables
    map<shared_ptr<BasePointer>, set<shared_ptr<IndexVariable>>> BPtoIdx;

    // our first step is to find and map all geps in the task first
    // find: search for each gep
    //  - we do this by finding the "start points" of the search (that is, the points in the DFG we would expect to be using the products of geps that have worked together)
    //    -> starting points
    //       1. first instruction in the function group: they typically use dereferenced pointers
    //       2. stores. Their pointer operands likely used geps 
    // map: find out which geps work together and in which order..
    //  e.g. ld -> gep0 -> ld -> gep1 -> ld -> <function group> 
    //   - this has gep0 and gep1 working together to offset the original BP
    // when deciding the hierarchical relationship of indexVar's that map 1:1 with geps, we need a strict ordering of offsets
    // thus, we record both which geps have a relationship and in what order those relationships are defined in 
    // each hierarchy in this set is sorted from parent-most to child-most
    set<vector<shared_ptr<Graph::Inst>>> gepHierarchies;
    // our start points
    set<shared_ptr<Graph::Inst>> startPoints;
    for( const auto& c : t->getCycles() )
    {
        for ( const auto& b : c->getBody() )
        {
            for( const auto& i : b->instructions )
            {
                if( i->isFunction() )
                {
                    for( const auto& pred : i->getPredecessors() )
                    {
                        if( const auto& predInst = dynamic_pointer_cast<Graph::Inst>(pred->getSrc()) )
                        {
                            if( !predInst->isFunction() && (predInst->isMemory()) )
                            {
                                startPoints.insert(predInst);
                            }
                        }
                    }
                }
                else if( i->getOp() == Graph::Operation::store )
                {
                    // stores have two operands: value and pointer
                    // we are only interested in the pointer operand, so only pick that one has a starting point
                    for( const auto& pred : i->getPredecessors() )
                    {
                        if( const auto& predInst = dynamic_pointer_cast<Graph::Inst>(pred->getSrc()) )
                        {
                            if( predInst->isMemory() )
                            {
                                startPoints.insert(predInst);
                            }
                        }
                    }
                }
            }
        }
    }
    for( const auto& s : startPoints )
    {
        // now try to ascertain which geps are related and in what order they work
        // "current" is the gep that was last seen. When a new gep is encountered durin the DFG walk, "current" is its child
        shared_ptr<Graph::Inst> current = nullptr;
        // ordering of geps that have a relationship, from parent-most (front) to child-most (back)
        vector<shared_ptr<Graph::Inst>> ordering;
        deque<shared_ptr<Graph::Inst>> Q;
        set<shared_ptr<Graph::GraphNode>> covered;
        Q.push_front(s);
        // this loop walks backwards through the DFG, meaning we see child geps first, then their parent(s) later
        while( !Q.empty() )
        {
            if( Q.front()->getOp() == Graph::Operation::gep )
            {
                // this logic pushes the discovered gep (Q.front()) before "current", which sorts "ordering" from parent-most to child-most
                auto currentPos = std::find(ordering.begin(), ordering.end(), current);
                ordering.insert(currentPos, Q.front());
                current = Q.front();
            }
            for( const auto& op : Q.front()->getPredecessors() )
            {
                if( covered.find(op->getSrc()) != covered.end() )
                {
                    continue;
                }
                else if( dynamic_pointer_cast<Graph::Inst>(op->getSrc()) == nullptr )
                {
                    continue;
                }
                else if( !t->find(static_pointer_cast<Graph::DataValue>(op->getSrc())) )
                {
                    // ben 2023-09-25 there are base pointer offsets within serial code that we need to track
                    //continue;
                }
                const auto& opInst = static_pointer_cast<Graph::Inst>(op->getSrc());
                if( opInst->getOp() == Graph::Operation::load )
                {
                    // loads and geps can work together to offset mutli-dimensional arrays
                    Q.push_back(opInst);
                    covered.insert(opInst);
                }
                else if( opInst->getOp() == Graph::Operation::gep )
                {
                    Q.push_back(opInst);
                    covered.insert(opInst);
                }
                else if( opInst->isBinaryOp() )
                {
                    // we don't record these but they may lead us to other things
                    Q.push_back(opInst);
                    covered.insert(opInst);
                }
                else if( opInst->isCastOp() )
                {
                    // we don't record these but they may lead us to other things
                    Q.push_back(opInst);
                    covered.insert(opInst);
                }
            }
            Q.pop_front();
        }
        gepHierarchies.insert(ordering);
    }
    // next, gather information for each gep and construct an indexVariable for it 
    // each gep will result in one or more indexVariables
    // information:
    //  1. what is its source (does it come from the heap via load? does it come from a phi? does it come from another gep?)
    //     - useful for mapping base pointers and induction variables to idxVars
    //  2. which binary operators touch it?
    //     - this gives insight into which "dimension" of the polyhedral space this indexVariable works within
    // it is possible for multiple geps to use the same binary operations, thus we make the covered set out here to avoid redundancy
    set<const llvm::Value*> covered;
    // this map helps us avoid redundant idxVars
    map<const shared_ptr<Cyclebite::Graph::Inst>, shared_ptr<IndexVariable>> nodeToIdx;
    for( const auto& gh : gepHierarchies )
    {
        // for each gep in the hierarchy, we figure out 
        // in the case of hierarchical geps, we have to construct all objects first before assigning hierarchical relationships
        // thus, we make a vector of them here and assign their positions later
        // the vector is reverse-sorted (meaning children are first in the list, parents last) 
        vector<shared_ptr<IndexVariable>> hierarchy;
        // remember gh is in hierarchy order (parent-most first, child-most last)
        for( const auto& gep : gh )
        {
            deque<const llvm::Value*> Q;
            // records binary operations found to be done on gep indices
            // used to investigate the dimensionality of the pointer offset being done by the gep
            // the ordering of the ops is done in reverse order (since the DFG traversal is reversed), thus the inner-most dimension is first in the list, outer-most is last 
            vector<pair<const llvm::Instruction*, AffineOffset>> bins;
            // holds the set of values that "source" the indexVariable
            // these are used later to find out how this indexVariable is connected to others
            set<const llvm::Value*> sources;
            covered.insert(gep->getInst());
            for( const auto& idx : llvm::cast<llvm::GetElementPtrInst>(gep->getInst())->indices() )
            {
                Q.push_front(idx);
                covered.insert(idx);
            }
            while( !Q.empty() )
            {
                // The indices of the gep represent the offset done on its pointer operand, and contain all the information of an idxVar
                // - there are several cases that must be accounted for
                //   -> constant: simplest idxVar of them all, commonly used in color-encoded images (to select r, g or b)
                //   -> binary ops: may represent the combination of multiple dimensions, e.g., var0*SIZE + var1
                //   -> cast: this is LLVM IR plumbing, ignore
                //   -> instructions that terminate the DFG walk: finding the source pointer being offset, another gep (which represents another idx var), or a PHI (which may map to an IV)
                if( const auto& con = llvm::dyn_cast<llvm::Constant>(Q.front()) )
                {
                    // a constant is likely a simple offset on a structure, that's useful
                    AffineOffset of;
                    if( con->getType()->isIntegerTy() )
                    {
                        of.constant = (int)*con->getUniqueInteger().getRawData();
                        of.transform = Cyclebite::Graph::Operation::add;
                    }
                    // floating point offsets shouldn't happen
                    else 
                    {
                        throw CyclebiteException("Cannot handle a memory offset that isn't an integer!");
                    }
                }
                else if( const auto& bin = llvm::dyn_cast<llvm::BinaryOperator>(Q.front()) )
                {
                    AffineOffset of;
                    of.transform = Graph::GetOp(bin->getOpcode());
                    bins.push_back(pair(bin, of));
                    // look for a constant that I can tie to this
                    for( const auto& pred : bin->operands() )
                    {
                        if( const auto& con = llvm::dyn_cast<llvm::Constant>(pred) )
                        {
                            if( con->getType()->isIntegerTy() )
                            {
                                of.constant = (int)*con->getUniqueInteger().getRawData();
                            }
                            // floating point offsets shouldn't happen
                            else 
                            {
                                throw CyclebiteException("Cannot handle a memory offset that isn't an integer!");
                            }   
                            bins.back().second = of;
                        }
                        else
                        {
                            of.constant = 0; // undeterminable
                            bins.back().second = of;
                            Q.push_back(pred);
                            covered.insert(pred);
                        }
                    }
                }
                else if( const auto& cast = llvm::dyn_cast<llvm::CastInst>(Q.front()) )
                {
                    // do nothing, we don't care about cast operators they are just LLVM IR plumbing
                    for( const auto& op : cast->operands() )
                    {
                        if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(op) )
                        {
                            Q.push_back(op);
                            covered.insert(op);
                        }
                    }
                }
                else if( const auto& phi = llvm::dyn_cast<llvm::PHINode>(Q.front()) )
                {
                    // if this is the index itself, it is an index variable
                    // if the phi is transformed by binary ops from above, it is not an idxVar
                    bool phiIsIndex = false;
                    for( const auto& idx : llvm::cast<llvm::GetElementPtrInst>(gep->getInst())->indices() )
                    {
                        if( phi == idx.get() )
                        {
                            phiIsIndex = true;
                        }
                    }
                    if( !phiIsIndex )
                    {
                        Q.pop_front();
                        covered.insert(phi);
                        continue;
                    }                    
                    AffineOffset of;
                    // when induction variables are combined into a single gep to make a multi-dimensional access, we need to capture this with an idxVar for each index
                    shared_ptr<InductionVariable> var = nullptr;
                    for( const auto& v : vars )
                    {
                        if( v->getNode()->getVal() == phi )
                        {
                            var = v;
                            break;
                        }
                    }
                    if( var )
                    {
                        of.constant = (int)var->getSpace().stride;
                        of.transform = var->getSpace().min < var->getSpace().max ? Graph::Operation::add : Graph::Operation::sub;
                    }
                    else
                    {
                        // we don't know what the affine offset is (for sure), so just push + 1
                        of.constant = 1;
                        of.transform = Cyclebite::Graph::Operation::add;
                    }
                    bins.push_back( pair(phi, of) );
                }
                else if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
                {
                    // llvm front-end can do weird things in the new versions, like load from a multi-star pointer many times to get down to a more elementary array element
                    // e.g., if I have float a[x][y][z] aka float***, then the LLVM front end will get to float* by doing: float** b = load a, float* c = load b
                    // thus we need to walk through loads now - push the pointer operand into the q
                    if( const auto& ptrInst = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
                    {
                        Q.push_back(ptrInst);
                        covered.insert(ptrInst);
                    } 
                }
                Q.pop_front();
            }
            // next, we build out all idxVars that can be discovered from the indices of this gep
            deque<shared_ptr<IndexVariable>> idxVarOrder;
            if( bins.empty() ) 
            {
                // confirm that this gep is not already explained by existing indexVariables
                bool found = false;
                for( const auto& idx : llvm::cast<llvm::GetElementPtrInst>(gep->getInst())->indices() )
                {
                    for( const auto& idxVar : idxVars )
                    {
                        if( idxVar->getNode()->getInst() == idx.get() )
                        {
                            found = true;
                            break;
                        }
                    }
                    if( found )
                    {
                        // this gep has already been explained, move on
                        break;
                    }
                }
                if( found )
                {
                    continue;
                }
                // there should be a 1:1 map between indexVar and the gep
                // the ordering of geps has already been encoded in the hierarchy, thus we just push to the hierarchy list the indexVariable the represents this gep and move on
                shared_ptr<IndexVariable> newIdx = nullptr;
                if( nodeToIdx.find(gep) != nodeToIdx.end() )
                {
                    newIdx = nodeToIdx.at(gep);
                }
                else
                {
                    newIdx = make_shared<IndexVariable>(gep);
                    nodeToIdx[ gep ] = newIdx;
                }
                // if this is the first entry in the gep hierarchy, we are the parent-most, and thus updating the child will point us to the child
                // else, we need to update both ourselves and our parent
                if( gh.size() > 1 )
                {
                    if( gep != gh.front())
                    {
                        // there should be a parent gep to the current one - find it and update it
                        auto parentGep = prev( std::find(gh.begin(), gh.end(), gep) );
                        shared_ptr<IndexVariable> p = nullptr;
                        for( const auto& idx : idxVars )
                        {
                            // if the idxVar is a binaryOp, we won't find it by our parent gep
                            // thus we have to find it by searching through its geps (which may be the idxVar itself)
                            auto idxVarGeps = idx->getGeps();
                            if( idxVarGeps.find(*parentGep) != idxVarGeps.end() )
                            {
                                p = idx;
                            }
                        }
                        if( !p )
                        {
                            for( const auto& idx : idxVars )
                            {
                                PrintVal(idx->getNode()->getVal());
                            }
                            for( const auto& gep : gh )
                            {
                                PrintVal(gep->getInst());
                            }
                            PrintVal(gep->getInst());
                            throw CyclebiteException("Could not find parent idxVar!");
                        }
                        p->addChild(newIdx);
                        newIdx->setParent(p);
                    }
                }
                idxVarOrder.push_back( newIdx );
            }
            else if( bins.size() == 1 )
            {
                // 1:1 mapping between indexVar and this binary operator
                shared_ptr<IndexVariable> newIdx = nullptr;
                if( nodeToIdx.find( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(bins.begin()->first)) ) != nodeToIdx.end() )
                {
                    newIdx = nodeToIdx.at( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(bins.begin()->first)) );
                }
                else
                {
                    newIdx = make_shared<IndexVariable>( static_pointer_cast<Graph::Inst>(Graph::DNIDMap.at(bins.begin()->first)) );
                    nodeToIdx[ static_pointer_cast<Graph::Inst>(Graph::DNIDMap.at(bins.begin()->first)) ] = newIdx;
                }
                if( gh.size() > 1 )
                {
                    if( gep != *gh.begin() )
                    {
                        auto parentGep = prev( std::find(gh.begin(), gh.end(), gep) );
                        shared_ptr<IndexVariable> p = nullptr;
                        for( const auto& idx : idxVars )
                        {
                            // if the idxVar is a binaryOp, we won't find it by our parent gep
                            // thus we have to find it by searching through its geps (which may be the idxVar itself)
                            auto idxVarGeps = idx->getGeps();
                            if( idxVarGeps.find(*parentGep) != idxVarGeps.end() )
                            {
                                p = idx;
                            };
                        }
                        if( p )
                        {
                            p->addChild(newIdx);
                            newIdx->setParent(p);
                        }
                    }
                }
                idxVarOrder.push_back(newIdx);
            }
            else
            {
                // for each binary operation we encountered, it may or may not warrant an indexVariable
                // cases:
                // 1. multiply: this undoubtedly requires an indexVariable, because it makes an affine transformation on the index space
                // 2. add: warrants an index variable
                // 3. or (in the case of optimizer loop unrolling): does not warrant an index variable
                //    - when the add is summing two integers together, and both inputs come from the 
                for( auto bin = bins.rbegin(); bin <= prev(bins.rend()); bin++ )
                {
                    if( bin == bins.rbegin() )
                    {
                        shared_ptr<IndexVariable> newIdx = nullptr;
                        if( nodeToIdx.find( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(bin->first)) ) != nodeToIdx.end() )
                        {
                            newIdx = nodeToIdx.at( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(bin->first)) );
                        }
                        else
                        {
                            newIdx = make_shared<IndexVariable>( static_pointer_cast<Graph::Inst>(Graph::DNIDMap.at(bin->first)) );
                            nodeToIdx[ static_pointer_cast<Graph::Inst>(Graph::DNIDMap.at(bin->first)) ] = newIdx;
                        }
                        shared_ptr<IndexVariable> child = nullptr;
                        if( nodeToIdx.find( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(next(bin)->first)) ) != nodeToIdx.end() )
                        {
                            child = nodeToIdx.at( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(next(bin)->first)) );
                        }
                        else
                        {
                            child = make_shared<IndexVariable>( static_pointer_cast<Graph::Inst>(Graph::DNIDMap.at(next(bin)->first)), newIdx );
                            nodeToIdx[ static_pointer_cast<Graph::Inst>(Graph::DNIDMap.at(next(bin)->first)) ] = child;
                        }
                        newIdx->addChild(child);
                        idxVarOrder.push_back(newIdx);
                        idxVarOrder.push_back(child);
                        bin = next(bin);
                    }
                    else if( bin == prev(bins.rend()) )
                    {
                        // case where there are an odd number of entries
                        // then just create the last idxVar and update the hierarchy
                        shared_ptr<IndexVariable> newIdx = nullptr;
                        if( nodeToIdx.find( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(bin->first)) ) != nodeToIdx.end() )
                        {
                            newIdx = nodeToIdx.at( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(bin->first)) );
                        }
                        else
                        {
                            newIdx = make_shared<IndexVariable>( static_pointer_cast<Graph::Inst>(Graph::DNIDMap.at(bin->first)), idxVarOrder.back() );
                            nodeToIdx[ static_pointer_cast<Graph::Inst>(Graph::DNIDMap.at(bin->first)) ] = newIdx;
                        }
                        idxVarOrder.back()->addChild(newIdx);
                        idxVarOrder.push_back(newIdx);
                        // don't increment the iterator, the loop will do that for us
                    }
                    else if( bin >= bins.rend() )
                    {
                        // we are done
                    }
                    else
                    {
                        // default case
                        shared_ptr<IndexVariable> newIdx = nullptr;
                        if( nodeToIdx.find( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(bin->first)) ) != nodeToIdx.end() )
                        {
                            newIdx = nodeToIdx.at( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(bin->first)) );
                        }
                        else
                        {
                            newIdx = make_shared<IndexVariable>( static_pointer_cast<Graph::Inst>(Graph::DNIDMap.at(bin->first)), idxVarOrder.back());
                            nodeToIdx[ static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(bin->first)) ] = newIdx;
                        }
                        shared_ptr<IndexVariable> child = nullptr;
                        if( nodeToIdx.find( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(next(bin)->first)) ) != nodeToIdx.end() )
                        {
                            child = nodeToIdx.at( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(next(bin)->first)) );
                        }
                        else
                        {
                            child = make_shared<IndexVariable>( static_pointer_cast<Graph::Inst>(Graph::DNIDMap.at(next(bin)->first)), newIdx );
                            nodeToIdx [ static_pointer_cast<Graph::Inst>(Graph::DNIDMap.at(next(bin)->first)) ] = newIdx;
                        }
                        newIdx->addChild(child);
                        idxVarOrder.back()->addChild(newIdx);
                        idxVarOrder.push_back(newIdx);
                        idxVarOrder.push_back(child);
                        bin = next(bin);
                    }
                }
            }
            // after the gep indices are done, we investigate the pointer operand of the gep
            // the pointer of the gep currently being investigated may be getting offset by another gep
            // but we haven't accounted for this yet, thus we investigate the pointer here and draw an edge between the idxVar(s) made from the indices and the idxVars that offset our gep's pointer 
            deque<const llvm::Instruction*> instQ;
            set<const llvm::Instruction*> instCovered;
            if( const auto& inst = llvm::dyn_cast<llvm::Instruction>( llvm::cast<llvm::GetElementPtrInst>(gep->getInst())->getPointerOperand()) )
            {
                instQ.push_front(inst);
                instCovered.insert(inst);
            }
            // set of geps that may be unconnected parents to the gep under investigation
            set<shared_ptr<IndexVariable>> parents;
            while( !instQ.empty() )
            {
                if( const auto& gep = llvm::dyn_cast<llvm::GetElementPtrInst>(instQ.front()) )
                {
                    // if the gep under investigation maps to a parent through its pointer, that parent should already exist (since we evaluate geps in parent-to-child order)
                    for( const auto& idx : idxVars )
                    {
                        if( idx->getNode()->getInst() == gep )
                        {
                            parents.insert(idx);
                        }
                    }
                }
                else
                {
                    for( const auto& op : instQ.front()->operands() )
                    {
                        if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(op) )
                        {
                            if( instCovered.find(inst) == instCovered.end() )
                            {
                                instQ.push_back(inst);
                                instCovered.insert(inst);
                            }
                        }
                    }
                }
                instQ.pop_front();
            }
            if( parents.size() > 1 )
            {
                PrintVal(gep->getInst());
                for( const auto& p : parents )
                {
                    PrintVal(p->getNode()->getInst());
                }
                throw CyclebiteException("Cannot yet handle the case where an idxVar has multiple parents!");
            }
            for( auto& p : parents )
            {
                // these are the parents of the parent-most idxVar(s) in idxVarOrder
                shared_ptr<IndexVariable> parentMost = nullptr;
                for( const auto& idxVar : idxVarOrder )
                {
                    if( !idxVar->getParent() )
                    {
                        if( parentMost )
                        {
                            throw CyclebiteException("Found more than one parent-most idxVar for a single gep!");
                        }
                        parentMost = idxVar;
                    }
                }
                if( !parentMost )
                {
                    // this means we already covered the edge, so continue
                    continue;
                }
                if( parentMost->getParent() )
                {
                    if( parentMost->getParent() != p )
                    {
                        throw CyclebiteException("Parent-most idxVar of a gep already has a parent, but also maps to a parent gep through its pointer operand!");
                    }
                }
                parentMost->setParent(p);
                p->addChild(parentMost);
            }

            // add all the new idxVars to the set
            for( const auto& idx : idxVarOrder )
            {
                idxVars.insert(idx);
            }
        }
    }
    // find sources to each var
    for( const auto& idx : idxVars )
    {
        deque<const llvm::Instruction*> Q;
        set<const llvm::Instruction*> covered;
        // find out if this index var is using an induction variable;
        for( const auto& iv : vars )
        {
            if( iv->isOffset(idx->getNode()->getInst()) )
            {
                idx->setIV(iv);
                break;
            }
        }
        // map idxVars to their base pointer(s)
        for( const auto& bp : BPs )
        {
            for( const auto& gep : idx->getGeps() )
            {
                if( bp->isOffset(gep->getInst()) )
                {
                    idx->addBP(bp);
                }
            }
        }
    }
    // determine if a non-leaf node in the idxVar tree is by itself loaded
    for( const auto& idx : idxVars )
    {
        if( !idx->getChildren().empty() )
        {
            // we evaluate its uses. Each one must satisfy the following:
            // 1. it must have a gep that has no offset (or be a gep itself)
            // 2. the gep must be used by a load 
            // 3. the load must not have any memory-group uses
            bool noBadUses = true;
            set<const llvm::GetElementPtrInst*> geps;
            set<const llvm::LoadInst*> lds;
            if( const auto& g = llvm::dyn_cast<llvm::GetElementPtrInst>(idx->getNode()->getInst()) )
            {
                geps.insert( g );
            }
            else
            {
                for( const auto& use : idx->getNode()->getInst()->users() )
                {
                    if( const auto& useInst = llvm::dyn_cast<llvm::Instruction>(use) )
                    {
                        if( const auto& g = llvm::dyn_cast<llvm::GetElementPtrInst>(use) )
                        {
                            geps.insert( g );
                        }
                        else
                        {
                            deque<const llvm::Instruction*> Q;
                            Q.push_front(useInst);
                            while( !Q.empty() )
                            {
                                if( const auto& cast = llvm::dyn_cast<llvm::CastInst>(Q.front()) )
                                {
                                    for( const auto& castUse : cast->users() )
                                    {
                                        if( const auto& castUseInst = llvm::dyn_cast<llvm::Instruction>(castUse) )
                                        {
                                            Q.push_back(castUseInst);
                                        }
                                    }
                                }
                                else if( const auto& g = llvm::dyn_cast<llvm::GetElementPtrInst>(Q.front()) )
                                {
                                    geps.insert(g);
                                }
                                Q.pop_front();
                            }
                        }
                    }
                }
            }
            for( const auto& gep : geps )
            {
                for( const auto& use : gep->users() )
                {
                    if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(use) )
                    {
                        lds.insert(ld);
                    }
                }
            }
            if( lds.empty() )
            {
                // geps may lead to function calls, and in that case we want a collection for the memory that has been offset
                //idx->setLoaded(true);
                noBadUses = false;
            }
            else
            {
                for( const auto& ld : lds )
                {
                    for( const auto& use : ld->users() )
                    {
                        if( const auto& useInst = llvm::dyn_cast<llvm::Instruction>(use) )
                        {
                            auto node = static_pointer_cast<Graph::Inst>(Graph::DNIDMap.at(useInst));
                            if( node->isMemory() )
                            {
                                noBadUses = false;
                                break;
                            }
                        }
                    }
                    if( !noBadUses )
                    {
                        break;
                    }
                }
            }
            if( noBadUses )
            {
                idx->setLoaded(true);
            }
        }
    }
#ifdef DEBUG
    auto dotString = PrintIdxVarTree(idxVars);
    ofstream tStream("IdxVarTree_Task"+to_string(t->getID())+".dot");
    tStream << dotString;
    tStream.close();
#endif
    return idxVars;
}