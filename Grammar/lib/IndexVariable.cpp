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

IndexVariable::IndexVariable( const std::shared_ptr<Cyclebite::Graph::DataValue>& n, 
                              const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& p, 
                              const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& c ) : Symbol("idx"), node(n), parent(p), child(c) {}

void IndexVariable::setChild( const shared_ptr<IndexVariable>& c )
{
    child = c;
}

void IndexVariable::setParent( const shared_ptr<IndexVariable>& p)
{
    parent = p;
}

void IndexVariable::setIV( const shared_ptr<InductionVariable>& indVar )
{
    iv = indVar;
}

void IndexVariable::setBP( const shared_ptr<BasePointer>& baseP )
{
    bp = baseP;
}

const shared_ptr<Cyclebite::Graph::DataValue>& IndexVariable::getNode() const
{
    return node;
}

const shared_ptr<Cyclebite::Grammar::IndexVariable>& IndexVariable::getParent() const
{
    return parent;
}

const shared_ptr<Cyclebite::Grammar::IndexVariable>& IndexVariable::getChild() const
{
    return child;
}

const shared_ptr<InductionVariable>& IndexVariable::getIV() const
{
    return iv;
}

const shared_ptr<BasePointer>& IndexVariable::getBP() const
{
    return bp;
}

string IndexVariable::dump() const
{
    return name;
}

const PolySpace IndexVariable::getSpace() const
{
    return space;
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
    //   - this has gep0 and gep1 working together to offset the original BP, thus 
    // when deciding the hierarchical relationship of indexVar's that map 1:1 with geps, we need a strict ordering of offsets
    // thus, we record both which geps have a relationship and in what order those relationships are defined in 
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
                    bool noFunction = true;
                    for( const auto& pred : i->getPredecessors() )
                    {
                        if( const auto& predInst = dynamic_pointer_cast<Graph::Inst>(pred->getSrc()) )
                        {
                            if( !predInst->isFunction() )
                            {
                                noFunction = false;
                                break;
                            }
                        }
                    }
                    if( noFunction )
                    {
                        startPoints.insert(i);
                    }
                }
                else if( i->getOp() == Graph::Operation::store )
                {
                    startPoints.insert(i);
                }
            }
        }
    }
    for( const auto& s : startPoints )
    {
        // now try to ascertain which geps are related and in what order they work
        // "current" is the gep that was last seen. It allows for relationships to be forged between geps
        shared_ptr<Graph::Inst> current = nullptr;
        // ordering is the order of geps (in reverse order) that have been found to work together
        vector<shared_ptr<Graph::Inst>> ordering;
        deque<shared_ptr<Graph::Inst>> Q;
        set<shared_ptr<Graph::GraphNode>> covered;
        Q.push_front(s);
        while( !Q.empty() )
        {
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
                    continue;
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
                    auto currentPos = std::find(ordering.begin(), ordering.end(), current);
                    ordering.insert(currentPos, opInst);
                    current = opInst;
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
                    // we don't recorde these but they may lead us to other things
                    Q.push_back(opInst);
                    covered.insert(opInst);
                }
            }
            Q.pop_front();
        }
        gepHierarchies.insert(ordering);
    }
    // next, gather preliminary information for each gep and construct an indexVariable for it 
    // each gep will result in one or more indexVariables
    // information:
    //  1. what is its source (does it come from the heap via load? does it come from a phi? does it come from another gep?)
    //  2. which binary operators touch it?
    // mappings:
    // indexVar -> base pointer (who do I offset)
    // indexVar -> induction Variable (do I project onto the control space?)
    for( const auto& gh : gepHierarchies )
    {
        // for each gep in the hierarchy, we figure out what to do with it
        // in the case of hierarchical geps, we have to construct all objects first before assigning hierarchical relationships
        // thus, we make a vector of them here and assign their positions later
        // the vector is reverse-sorted (meaning children are first in the list, parents last) like "gh" is
        vector<shared_ptr<IndexVariable>> hierarchy;
        for( const auto& gep : gh )
        {
            deque<const llvm::Value*> Q;
            set<const llvm::Value*> covered;
            // records binary operations found to be done on gep indices, used to investigate the dimensionality of the pointer offset being done by the gep
            // the ordering of the ops is done in reverse order (since the DFG traversal is reversed), thus the inner-most dimension is first in the list, outer-most is last 
            vector<pair<const llvm::BinaryOperator*, AffineOffset>> bins;
            // holds the set of values that "source" the indexVariable
            // these are used later to find out how this indexVariable is connected to others
            set<const llvm::Value*> sources;
            for( const auto& idx : llvm::cast<llvm::GetElementPtrInst>(gep->getInst())->indices() )
            {
                Q.push_front(idx);
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
                    }
                    // floating point offsets shouldn't happen
                    else 
                    {
                        throw AtlasException("Cannot handle a memory offset that isn't an integer!");
                    }
                }
                else if( const auto& bin = llvm::dyn_cast<llvm::BinaryOperator>(Q.front()) )
                {
                    // look for a constant that I can tie to this
                    for( const auto& pred : bin->operands() )
                    {
                        AffineOffset of;
                        of.transform = Graph::GetOp(bin->getOpcode());
                        bins.push_back(pair(bin, of));
                        if( const auto& con = llvm::dyn_cast<llvm::Constant>(pred) )
                        {
                            if( con->getType()->isIntegerTy() )
                            {
                                of.constant = (int)*con->getUniqueInteger().getRawData();
                            }
                            // floating point offsets shouldn't happen
                            else 
                            {
                                throw AtlasException("Cannot handle a memory offset that isn't an integer!");
                            }   
                            bins.back().second = of;
                        }
                        else
                        {
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
                        }
                    }
                    Q.pop_front();
                }
                // When we reach the end of the idxVar information space, stop
                //  - possible places:
                //    -> another gep, this represents another dimension and therefore another idxVar
                //    -> PHI node, possibly maps to an induction variable
                //    -> ld, pointer possibly maps to a BP
                else if( const auto& gep = llvm::dyn_cast<llvm::GetElementPtrInst>(Q.front()) )
                {
                    // may map to something else, but we're done
                    sources.insert(gep);
                }
                else if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
                {
                    sources.insert(ld);
                }
                else if( const auto& phi = llvm::dyn_cast<llvm::PHINode>(Q.front()) )
                {
                    sources.insert(phi);
                }
                Q.pop_front();
            }
            
            /*// each binary operation maps to an indexVar
            // if there are 0, we still make one for the gep
            if( sources.size() != 1 )
            {
                PrintVal(gep);
                PrintVal(gep->getParent());
                throw AtlasException("Cannot yet handle the case where an IndexVariable has multiple sources!");
            }
            auto source = *sources.begin();*/
            if( bins.empty() ) 
            {
                // there should be a 1:1 map between indexVar and the gep
                // the ordering of geps has already been encoded in the hierarchy, thus we just push to the hierarchy list the indexVariable the represents this gep and move on
                hierarchy.push_back( make_shared<IndexVariable>(gep) );
            }
            else if( bins.size() == 1 )
            {

            }
            else
            {
                // for each binary operation we encountered, it may or may not warrant an indexVariable
                // cases:
                // 1. multiply: this undoubtedly requires an indexVariable, because it makes an affine transformation on the index space
                // 2. add: warrants an index variable
                // 3. or (in the case of optimizer loop unrolling): does not warrant an index variable
                //    - when the add is summing two integers together, and both inputs come from the 
                deque<shared_ptr<IndexVariable>> idxVarOrder;
                for( auto bin = bins.rbegin(); bin <= prev(bins.rend()); bin++ )
                {
                    if( bin == bins.rbegin() )
                    {
                        auto newIdx = make_shared<IndexVariable>( Graph::DNIDMap.at((*bin).first));
                        auto child  = make_shared<IndexVariable>( Graph::DNIDMap.at(next(bin)->first), newIdx );
                        newIdx->setChild(child);
                        idxVarOrder.push_back(newIdx);
                        idxVarOrder.push_back(child);
                        bin = next(bin);
                    }
                    else if( bin == prev(bins.rend()) )
                    {
                        // case where there are an odd number of entries
                        // then just create the last idxVar and update the hierarchy
                        auto newIdx = make_shared<IndexVariable>( Graph::DNIDMap.at(bin->first), idxVarOrder.back());
                        idxVarOrder.back()->setChild(newIdx);
                        idxVarOrder.push_back(newIdx);
                        // don't increment, the loop will do that for us
                    }
                    else if( bin >= bins.rend() )
                    {
                        // we are done
                    }
                    else
                    {
                        // default case
                        auto newIdx = make_shared<IndexVariable>( Graph::DNIDMap.at(bin->first), idxVarOrder.back());
                        auto child = make_shared<IndexVariable>( Graph::DNIDMap.at(next(bin)->first), newIdx);
                        newIdx->setChild(child);
                        idxVarOrder.back()->setChild(newIdx);
                        idxVarOrder.push_back(newIdx);
                        idxVarOrder.push_back(child);
                        bin = next(bin);
                    }
                }
                bool doNothing = true;
            }
            /*if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(source) )
            {
                
            }
            else if( const auto& gep = llvm::dyn_cast<llvm::GetElementPtrInst>(source) )
            {

            }
            else if( const auto& phi = llvm::dyn_cast<llvm::PHINode>(source) )
            {
            }
            else
            {
                PrintVal(gep);
                PrintVal(source);
                throw AtlasException("Cannot yet handle the case where an IndexVariable's source is this type!");
            }*/
        }
    }
    
    return idxVars;
}