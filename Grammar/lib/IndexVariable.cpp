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
#include "ReductionVariable.h"
#include <llvm/IR/Instructions.h>
#include "Util/Exceptions.h"
#include "Util/Print.h"
#include <spdlog/spdlog.h>

using namespace std;
using namespace Cyclebite::Grammar;

IndexVariable::IndexVariable( const shared_ptr<Cyclebite::Graph::DataValue>& n, 
                              const shared_ptr<Cyclebite::Graph::Inst>& i, 
                              const set<shared_ptr<Cyclebite::Grammar::IndexVariable>>& p, 
                              const set<shared_ptr<Cyclebite::Grammar::IndexVariable>>& c ) : Symbol("idx"), node(n), inst(i), parents(p), children(c) {}

IndexVariable::IndexVariable( const std::shared_ptr<Cyclebite::Graph::DataValue>& n, 
                              const shared_ptr<Cyclebite::Graph::Inst>& i, 
                              const shared_ptr<Cyclebite::Grammar::IndexVariable>& p, 
                              const set<shared_ptr<Cyclebite::Grammar::IndexVariable>>& c ) : Symbol("idx"), node(n), inst(i), children(c)
{
    if( p )
    {
        parents.insert(p);
    }
}

void IndexVariable::addChild( const shared_ptr<IndexVariable>& c )
{
    children.insert(c);
}

void IndexVariable::addParent( const shared_ptr<IndexVariable>& p)
{
    parents.insert(p);
}

void IndexVariable::addDimension( const shared_ptr<Dimension>& indVar )
{
    dims.insert(indVar);
    space = getSpace();
}

void IndexVariable::addOffsetBP( const shared_ptr<BasePointer>& p )
{
    offsetBPs.insert(p);
}

const shared_ptr<Cyclebite::Graph::DataValue>& IndexVariable::getNode() const
{
    return node;
}

const shared_ptr<Cyclebite::Graph::Inst>& IndexVariable::getInst() const
{
    return inst;
}

const set<shared_ptr<Cyclebite::Graph::Inst>, Cyclebite::Graph::p_GNCompare> IndexVariable::getGeps() const
{
    set<shared_ptr<Graph::Inst>, Graph::p_GNCompare> geps;
    deque<shared_ptr<Graph::Inst>> Q;
    set<shared_ptr<Graph::Inst>, Graph::p_GNCompare> covered;
    if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(node->getVal()) )
    {
        Q.push_front( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(inst)) );
        covered.insert( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(inst)) );
    }
    else if( const auto& con = llvm::dyn_cast<llvm::Constant>(node->getVal()) )
    {
        for( const auto& use : con->users() )
        {
            if( const auto& useInst = llvm::dyn_cast<llvm::Instruction>(use) )
            {
                Q.push_front( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(useInst)) );
                covered.insert( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(useInst)) );
            }
        }
    }
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

const set<shared_ptr<Cyclebite::Grammar::IndexVariable>>& IndexVariable::getParents() const
{
    return parents;
}

const set<shared_ptr<Cyclebite::Grammar::IndexVariable>>& IndexVariable::getChildren() const
{
    return children;
}

const set<shared_ptr<Dimension>>& IndexVariable::getDimensions() const
{
    return dims;
}

const set<shared_ptr<Dimension>> IndexVariable::getExclusiveDimensions() const
{
    set<shared_ptr<Dimension>> exclusive = dims;
    deque<shared_ptr<IndexVariable>> Q;
    set<shared_ptr<IndexVariable>> covered;
    Q.push_front(make_shared<IndexVariable>(*this));
    covered.insert(Q.front());
    while( !Q.empty() )
    {
        for( const auto& p : Q.front()->getParents() )
        {
            for( const auto& dim : p->getDimensions() )
            {
                exclusive.erase(dim);
            }
            if( !covered.contains(p) )
            {
                Q.push_back(p);
                covered.insert(p);
            }
        }
        Q.pop_front();
    }
    return exclusive;
}

const set<shared_ptr<BasePointer>>& IndexVariable::getOffsetBPs() const
{
    return offsetBPs;
}

string IndexVariable::dump() const
{
    return name;
}

string IndexVariable::printIdxVar(const map<shared_ptr<Symbol>, shared_ptr<Symbol>>& symbol2Symbol, const shared_ptr<InductionVariable>& var ) const
{
    string print = "";
    if( symbol2Symbol.contains(var) )
    {
        print += symbol2Symbol.at(var)->dumpHalide(symbol2Symbol);
        if( const auto& rv = dynamic_pointer_cast<ReductionVariable>(symbol2Symbol.at(var)) )
        {
            if( rv->getDimensions().size() > 1 )
            {
                // we need to get the position of this dimension in the reduction and add the appropriate Halide suffix 
                unsigned dimPosition = 0;
                for( const auto& dim : rv->getDimensions() )
                {
                    if( dim == var )
                    {
                        break;
                    }
                    else
                    {
                        dimPosition++;
                    }
                }
                switch(dimPosition)
                {
                    case 0: print += ".x"; break;
                    case 1: print += ".y"; break;
                    case 2: print += ".z"; break;
                    case 3: print += ".w"; break;
                    default: throw CyclebiteException("Cannot handle a reduction that is greater than four dimensions!");
                }
            }
        }
    }
    else
    {
        print += var->dumpHalide(symbol2Symbol);
        // put the shim for an extra dimension in the idxVar
        // an extra dimension can come up (for example, in a color image[y][x][c] - the colors are a 3rd dimension often unrolled and not mappable to a cycle)
        // this case is indicated by a gep that uses a constant offset to access members of a collection
        if( const auto& con = llvm::dyn_cast<llvm::Constant>( node->getVal()) )
        {
            int constIdx = -1;
            for( const auto& use : con->users() )
            {
                if( const auto& gep = llvm::dyn_cast<llvm::GetElementPtrInst>(use) )
                {
                    // this constant offset should be the last entry in the gep
                    if( gep->getNumIndices() )
                    {
                        if( const auto& con = llvm::dyn_cast<llvm::Constant>(prev(gep->idx_end())) )
                        {
                            // it must be an integer
                            if( con->getType()->isIntegerTy() )
                            {
                                constIdx = (int)*con->getUniqueInteger().getRawData();
                            }
                        }
                        // the second case here is when the gep is indexing the first member in the user-defined struct
                        // in that case, we confirm the gep is accessing a struct and make the accessing index 0
                        else if( gep->getSourceElementType()->isStructTy() )
                        {
                            // the gep is implicitly accessing the 0th member of the struct by omitting a constant offset into it
                            constIdx = 0; 
                        }
                    }
                    if( constIdx > -1 )
                    {
                        print += ", "+to_string(constIdx);
                    }
                }
            }
        }
    }
    return print;
}

string IndexVariable::dumpHalide( const map<shared_ptr<Symbol>, shared_ptr<Symbol>>& symbol2Symbol ) const
{
    // we are interested in printing the index variable in terms of its dimension
    // in this case we are interested in finding the child-most dimension in the indexVariable tree, then print that dimension plus any offset this indexVariable does to it
    // e.g., dim + offset ; dim * offset ; dim / offset ; etc.
    shared_ptr<Dimension> childMostDim = nullptr;
    // if this idxVar has its own exclusive dimension, we are done
    auto exclusives = getExclusiveDimensions();
    if( exclusives.size() == 1 )
    {
        childMostDim = *exclusives.begin();
        if( const auto& iv = dynamic_pointer_cast<InductionVariable>(*exclusives.begin()) )
        {
            auto foo = getOffset();
            auto print = printIdxVar( symbol2Symbol, iv );
            // the offset is necessary to add when our operation is not a multiply or divide
            if( foo.op != Cyclebite::Graph::Operation::mul && foo.op != Cyclebite::Graph::Operation::sdiv && foo.op != Cyclebite::Graph::Operation::udiv )
            {
                if( foo.coefficient != static_cast<int>(STATIC_VALUE::INVALID) && foo.coefficient != static_cast<int>(STATIC_VALUE::UNDETERMINED) )
                {
                    print += "+"+to_string(foo.coefficient);
                }
            }
            return print;
        }
        else
        {
            Cyclebite::Util::PrintVal(node->getVal());
            throw CyclebiteException("Cannot yet support dumping non-induction-variables to Halide");
        }
    }
    else if( exclusives.empty() )
    {
        // this index variable is offsetting a dimension somehow
        // second check is to see if we are using a binary operator on top of a dimension
        // first step is to find the dimension its modifying - the child-most dim at its point in the hierarchy tree
        deque<shared_ptr<IndexVariable>> Q;
        set<shared_ptr<IndexVariable>> covered;
        Q.push_front(make_shared<IndexVariable>(*this));
        covered.insert(Q.front());
        while( !Q.empty() )
        {
            for( const auto& p : Q.front()->getParents() )
            {
                if( p->getExclusiveDimensions().size() == 1 )
                {
                    childMostDim = *p->getExclusiveDimensions().begin();
                    break;
                }
                else
                {
                    Q.push_back(p);
                }
            }
            if( childMostDim )
            {
                break;
            }
            Q.pop_front();
        }
        if( !childMostDim )
        {
            if( getDimensions().empty() )
            {
                // this can be caused by base pointers being indexed from an anonymous structure created by LLVM for lambda function arguments, so just ignore it
                //Cyclebite::Util::PrintVal(node->getVal());
                ///throw CyclebiteException("Found an index variable that has zero dimensions underneath it!");
                return "";
            }
            else
            {
                Cyclebite::Util::PrintVal(node->getVal());
                throw CyclebiteException("Could not find dimension for index variable dump");
            }
        }
        else if( dynamic_pointer_cast<InductionVariable>(childMostDim) == nullptr )
        {
            Cyclebite::Util::PrintVal(node->getVal());
            throw CyclebiteException("Cannot yet support dumping non-induction-variables to Halide");
        }
        else
        {
            // since there is a child-most dimension, we know this is an inductionVariable we actually want to print
            // our second check is to see if it is a constant within a gep
            if( const auto& con = llvm::dyn_cast<llvm::Constant>(node->getVal()) )
            {
                if( const auto& gep = llvm::dyn_cast<llvm::GetElementPtrInst>(inst->getInst()) )
                {
                    // constants within geps are treated as entirely new dimensions 
                    // (e.g., an image with colors is image[y][x][c], but the c is within a user-defined struct and indexed with constant offsets)
                    // we wish to print another dimension index with the constant held by this index variable
                    return to_string((int)*con->getUniqueInteger().getRawData());
                }
            }
        }
        
        // to generate the offset to the dimension expression, we evaluate the instruction that lies underneath this indexVariable
        // there are two cases here (as of 2024-01-28)
        // 1. GEPs will "join" the indices together
        //    - arises when the programmer statically defines the configuration of 2D memory
        //      -- e.g., double (*p)[SIZE] = (double (*)[SIZE])malloc(sizeof(double[SIZE][SIZE]))
        // thus to discover the "offset" of idxVars we need to first find how they are used in these geps
        if( const auto& bin = llvm::dyn_cast<llvm::BinaryOperator>(getNode()->getVal()) )
        {
            // binary ops are the easiest case
            // find the constant in the expression
            auto offset = getOffset();
            if( offset.coefficient == static_cast<int>(STATIC_VALUE::UNDETERMINED) )
            {
                spdlog::warn("Could not determine the offset of index variable "+name);
                offset.coefficient = 0;
            }
            // BW 2024-02-16 this breaks in IIRBlur/Naive when the row blur multiplies the row index of the array (the binary instructions is a multiply, we actually want the add offset, which comes in a later instruction
            string ret = "";
            if( symbol2Symbol.contains( static_pointer_cast<InductionVariable>(childMostDim) ) )
            {
                ret += symbol2Symbol.at( static_pointer_cast<InductionVariable>(childMostDim) )->dumpHalide(symbol2Symbol);
            } 
            else
            {
                ret += static_pointer_cast<InductionVariable>(childMostDim)->dumpHalide(symbol2Symbol);
            }
            ret += Graph::OperationToString.at(Graph::GetOp(bin->getOpcode()))+to_string(offset.coefficient);
            return ret;
        }
        else if( node == childMostDim->getNode() )
        {
            // idxVars will match the dimension when the dimension is used exactly like a dimension
            // e.g., ptr = gep %bp, %dim0, %dim1
            // - this case is commonly found when the memory configuration of the array is statically-defined (as stated above)
            // thus we just need to print the dimension, and we are done!
            return name;
        }
    }
    else if( exclusives.size() == 2 )
    {
        // this is an example of an idxVar that combines two dimensions together using affine transformations
        // in this case our goal is to combine these dimensions in their order
        set<shared_ptr<InductionVariable>, DimensionSort> vars;
        for( const auto& e : exclusives )
        {
            if( const auto& iv = dynamic_pointer_cast<InductionVariable>(e) )
            {
                vars.insert(iv);
            }
        }
        // there is likely an operator between these two, and it should be implied in the DFG somewhere
        // to find it, we walk the DFG until we find an instruction that combines the dimensions together
        const llvm::BinaryOperator* combiner = nullptr;
        deque<const llvm::Instruction*> Q;
        set<const llvm::Instruction*> covered;
        if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(node->getVal()) )
        {
            Q.push_front(inst);
            covered.insert(inst);
        }
        else if( const auto& con = llvm::dyn_cast<llvm::Constant>(node->getVal()) )
        {
            for( const auto& use : con->users() )
            {
                if( const auto& useInst = llvm::dyn_cast<llvm::Instruction>(use) )
                {
                    Q.push_front(useInst);
                    covered.insert(useInst);
                }
            }
        }
        while( !Q.empty() )
        {
            for( const auto& op : Q.front()->operands() )
            {
                if( const auto& bin = llvm::dyn_cast<llvm::BinaryOperator>(op) )
                {
                    // this is the operator we are searching for
                    // confirm it is the one by confirming both operands are our exclusive dimensions
                    set<shared_ptr<InductionVariable>, DimensionSort> toEliminate;
                    for( const auto& op : bin->operands() )
                    {
                        for( const auto& v : vars )
                        {
                            if( op == v->getNode()->getVal() )
                            {
                                toEliminate.insert(v);
                                break;
                            }
                        }
                    }
                    if( toEliminate == vars )
                    {
                        // we have found the op we were looking for
                        combiner = bin;
                        break;
                    }
                }
                else
                {
                    if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(op) )
                    {
                        if( !covered.contains(inst) )
                        {
                            Q.push_back(inst);
                            covered.insert(inst);
                        }
                    }
                }
            }
            if( combiner )
            {
                break;
            }
            else
            {
                Q.pop_front();
            }
        }
        if( !combiner )
        {
            throw CyclebiteException("Could not combine a multi-dimensional idxVar into a cohesive expression!");
        }
        // with the operation to combine them, we can now make the print
        string print = printIdxVar( symbol2Symbol, *vars.begin() );
        print += string(Cyclebite::Graph::OperationToString.at(Cyclebite::Graph::GetOp(combiner->getOpcode())));
        print += printIdxVar( symbol2Symbol, *next(vars.begin()) );
        return print;
    }
    return name;
}

const PolySpace IndexVariable::getSpace() const
{
    PolySpace space;
    auto exDims = getExclusiveDimensions();
    if( exDims.empty() )
    {
        // this is a special case where we make an affine transformation to a parent idxVar, thus we have no exclusive dimension
        // we take the exclusive dimensions(s) of our parent
        for( const auto& p : parents )
        {
            for( const auto& dim : p->getExclusiveDimensions() )
            {
                exDims.insert(dim);
            }
        }
    }
    for( const auto& dim : exDims )
    {
        if( const auto& iv = dynamic_pointer_cast<Counter>(dim) )
        {
            // combining spaces is simply done by intersection
            // thus, we take the greatest min, the least max, and the least stride
            if( space.min == static_cast<int>(STATIC_VALUE::INVALID) )
            {
                space.min = iv->getSpace().min;
            }
            else if( space.min < iv->getSpace().min )
            {
                space.min = iv->getSpace().min;
            }
            if( space.max == static_cast<int>(STATIC_VALUE::INVALID) )
            {
                space.max = iv->getSpace().max;
            }
            else if( space.max > iv->getSpace().max )
            {
                space.max = iv->getSpace().max;
            }
            if( space.stride == static_cast<int>(STATIC_VALUE::INVALID) )
            {
                space.stride = iv->getSpace().stride;
            }
            else if( space.stride > iv->getSpace().stride )
            {
                space.stride = iv->getSpace().stride;
            }
        }
    }
    return space;
}

bool IndexVariable::isValueOrTransformedValue(const llvm::Instruction* i, const llvm::Value* v) const
{
    return i == inst->getInst() && v == node->getVal();
    /*
    else if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(node->getVal()) )
    {
        // to build a method that will only recognize the uses of a specific index variable, we have to be aware of all other index variables
        // then, when we walk the DFG, we can spot all idxVars, no matter where they lie or what relationship they are to us
        // the idxVar tree is mostly connected, so if we walk the idxVar tree we will (likely) acquire all idxVars we need to know about
        set<const llvm::Value*> forbidden;

        // it can be hard to detect the child idxVars of IVs when those IVs are not their parent (because they aren't used in the same gep)
        // thus we implement a check here specific to phis whose users are binary instructions
        if( const auto& phi = llvm::dyn_cast<llvm::PHINode>(node->getVal()) )
        {
            for( const auto& use : phi->users() )
            {
                if( const auto& bin = llvm::dyn_cast<llvm::BinaryOperator>(use) )
                {
                    forbidden.insert(bin);
                }
            }
        }
        // anonymous DFG namespace prevents DFG walk clash
        {
            deque<const IndexVariable*> Q;
            set<const IndexVariable*> covered;
            Q.push_front(this);
            covered.insert(this);
            while( !Q.empty() )
            {
                for( const auto& c : Q.front()->getChildren() )
                {
                    if( !covered.contains(c.get()) )
                    {
                        Q.push_back(c.get());
                        covered.insert(c.get());
                        if( c.get() != this )
                        {
                            forbidden.insert(c->getNode()->getVal());
                        }
                    }
                }
                for( const auto& p : Q.front()->getParents() )
                {
                    if( !covered.contains(p.get()) )
                    {
                        Q.push_back(p.get());
                        covered.insert(p.get());
                        if( p.get() != this )
                        {
                            forbidden.insert(p->getNode()->getVal());
                        }
                    }
                }
                Q.pop_front();
            }
        } // anonymous DFG namespace

        // trivial check
        if( forbidden.contains(v) )
        {
            return false;
        }
        deque<const llvm::Instruction*> Q;
        set<const llvm::Value*> covered;
        Q.push_front(inst);
        covered.insert(node->getVal());
        while( !Q.empty() )
        {
            if( Q.front() == v )
            {
                return true;
            }
            for( const auto& use : Q.front()->users() )
            {
                if( const auto& useInst = llvm::dyn_cast<llvm::Instruction>(use) )
                {
                    if( !forbidden.contains(useInst) )
                    {
                        if( !covered.contains(useInst) )
                        {
                            Q.push_back(useInst);
                            covered.insert(useInst);
                        }
                    }
                }
            }
            Q.pop_front();
        }
    }
    else if( const auto& con = llvm::dyn_cast<llvm::Constant>(node->getVal()) )
    {
        for( const auto& use : node->getVal()->users() )
        {
            if( use == v )
            {
                return true;
            }
        }
    }
    return false;
    */
}

bool IndexVariable::overlaps( const shared_ptr<IndexVariable>& var1 ) const
{
    // for two vars to overlap, three conditions must be satisfied
    // 0. Their strides are non-zero
    if( space.stride == 0 || var1->getSpace().stride == 0 )
    {
        return false;
    }
    // 1. Their boundaries intersect
    if( (space.min != static_cast<int>(STATIC_VALUE::UNDETERMINED)) && space.max != (static_cast<int>(STATIC_VALUE::UNDETERMINED)) )
    {
        if( (var1->getSpace().min != static_cast<int>(STATIC_VALUE::UNDETERMINED)) && (var1->getSpace().max != static_cast<int>(STATIC_VALUE::UNDETERMINED)) )
        {
            // everything is determined, we can find overlap easily
            if( space.min < var1->getSpace().min )
            {
                if( var1->getSpace().min > space.max )
                {
                    // there is no overlap at all
                    return false;
                }
            }
            else
            {
                // var1's min is greater than ours. Our max has to be at least greater than their min to have overlap
                if( space.max < var1->getSpace().min )
                {
                    return false;
                }
            }
        }
        else if( var1->getSpace().min != static_cast<int>(STATIC_VALUE::UNDETERMINED) )
        {
            // var1 minimum is known. We have no overlap if this minimum is greater than our maximum
            if( var1->getSpace().min > space.max )
            {
                return false;
            }
        }
        else if( var1->getSpace().max != static_cast<int>(STATIC_VALUE::UNDETERMINED) )
        {
            // var1 maximum is known. We have no overlap if this maximum is less than our minimum
            if( var1->getSpace().max < space.min )
            {
                return false;
            }
        }
    }
    else if( space.min != static_cast<int>(STATIC_VALUE::UNDETERMINED) )
    {
        // we only know our minimum
        if( (var1->getSpace().min != static_cast<int>(STATIC_VALUE::UNDETERMINED)) && (var1->getSpace().max != static_cast<int>(STATIC_VALUE::UNDETERMINED)) )
        {
            // we don't at all overlap with var1 if our minimum is greather than var1's max
            if( space.min > var1->getSpace().max )
            {
                return false;
            }
        }
        else if( var1->getSpace().min != static_cast<int>(STATIC_VALUE::UNDETERMINED) )
        {
            if( (space.min != var1->getSpace().min) || ( (space.stride < 0) != (var1->getSpace().stride < 0) ) )
            {
                // we know for sure our spaces cannot overlap because they either
                // 1. do not start at the same place
                // 2. do not stride in the same direction
                spdlog::warn("Vars "+dump()+" (min: "+to_string(space.min)+") and "+var1->dump()+" (min: "+to_string(var1->getSpace().min)+") did not have determined boundaries that could confirm overlap.");
                return false; 
            }
        }
        else if( var1->getSpace().max != static_cast<int>(STATIC_VALUE::UNDETERMINED) )
        {
            // we cannot determine overlap at all because we don't know how large the regions are
            spdlog::warn("Vars "+dump()+" (min: "+to_string(space.min)+") and "+var1->dump()+" (max: "+to_string(var1->getSpace().max)+") did not have determined boundaries that could confirm overlap.");
            return false; 
        }
    }
    else if( space.max != static_cast<int>(STATIC_VALUE::UNDETERMINED) )
    {
        // we only know our maximum
        if( (var1->getSpace().min != static_cast<int>(STATIC_VALUE::UNDETERMINED)) && (var1->getSpace().max != static_cast<int>(STATIC_VALUE::UNDETERMINED)) )
        {
            // we don't have any overlap if our max is less than var1's min
            if( space.max < var1->getSpace().min )
            {
                return false;
            }
        }
        else if( var1->getSpace().min != static_cast<int>(STATIC_VALUE::UNDETERMINED) )
        {
            // we cannot determine overlap at all because we don't know how large the regions are
            spdlog::warn("Vars "+dump()+" (max: "+to_string(space.max)+") and "+var1->dump()+" (min: "+to_string(var1->getSpace().min)+") did not have determined boundaries that could confirm overlap.");
            return false; 
        }
        else if( var1->getSpace().max != static_cast<int>(STATIC_VALUE::UNDETERMINED) )
        {
            if( (space.max != var1->getSpace().max) || ( (space.stride < 0) != (var1->getSpace().stride < 0) ) )
            {
                // we know for sure our spaces cannot overlap because they either
                // 1. do not start at the same place
                // 2. do not stride in the same direction
                spdlog::warn("Vars "+dump()+" (max: "+to_string(space.max)+") and "+var1->dump()+" (max: "+to_string(var1->getSpace().max)+") did not have determined boundaries that could confirm overlap.");
                return false;
            }
        }
    }
    else
    {
        // nothing is determined, we can't say anything about whether these spaces overlap
        spdlog::warn("Vars "+dump()+" and "+var1->dump()+" did not have determined boundaries that could confirm overlap.");
        return false;
    }

    // 2. They index the same dimension of the base pointer
    if( getDimensionIndex() != var1->getDimensionIndex() )
    {
        return false;
    }
    // 3. One index makes an affine offset that touches a previously-determined index
    // to determine what a "previously-determined" index is, we evaluate the stride pattern to find the ordering of integers in the space
    if( space.stride != static_cast<int>(STATIC_VALUE::UNDETERMINED) && var1->getSpace().stride != static_cast<int>(STATIC_VALUE::UNDETERMINED) )
    {
        if( space.stride != static_cast<int>(STATIC_VALUE::INVALID) && var1->getSpace().stride != static_cast<int>(STATIC_VALUE::INVALID) )
        {
            if( (getOffset().coefficient != static_cast<int>(STATIC_VALUE::UNDETERMINED)) && (var1->getSpace().stride != static_cast<int>(STATIC_VALUE::UNDETERMINED)) )
            {
                if( (getOffset().coefficient < 0) != (var1->getSpace().stride < 0) )
                {
                    // our coefficient goes against the stride, indicating that this var touches a previous integer in the var1 space; this is overlap
                    return true;
                }
            }
        }
        else
        {
            spdlog::warn("When overlapping "+dump()+" and "+var1->dump()+" the stride patterns were not valid.");
        }
    }
    else
    {
        spdlog::warn("When overlapping "+dump()+" and "+var1->dump()+" the stride patterns could not be determined.");
    }
    return false;
}

DimensionOffset IndexVariable::getOffset() const
{
    DimensionOffset dim;
    if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(node->getVal()) )
    {
        dim = { .op = Cyclebite::Graph::GetOp(inst->getOpcode()), .coefficient = static_cast<int>(Cyclebite::Grammar::STATIC_VALUE::UNDETERMINED) };
        // to find the coefficient associated with this operation
        for( const auto& op : inst->operands() )
        {
            if( const auto& con = llvm::dyn_cast<llvm::Constant>(op) )
            {
                if( con->getType()->isIntegerTy() )
                {
                    dim.coefficient = (int)*con->getUniqueInteger().getRawData();
                }
                else
                {
                    throw CyclebiteException("Found a non-integer in an idxVar!");
                }
            }
        }
    }
    else if( const auto& con = llvm::dyn_cast<llvm::Constant>(node->getVal()) )
    {
        // we assume the operation is an add because constant idxVars come from constants inside gep indices (which implicitly add)
        dim.op = Cyclebite::Graph::Operation::add;
        dim.coefficient = static_cast<int>(STATIC_VALUE::UNDETERMINED);
        if( con->getType()->isIntegerTy() )
        {
            dim.coefficient = (int)*con->getUniqueInteger().getRawData();
        }
    }
    else
    {
        throw CyclebiteException("Cannot yet handle an induction variable that is neither an instruction nor a constant!");
    }
    return dim;
}

int IndexVariable::getDimensionIndex() const
{
    deque<const IndexVariable*> Q;
    set<const IndexVariable*> covered;
    // the dimension index counts how many dimensions we encounter as we walk up the idxVar tree
    set<shared_ptr<Dimension>> dimensions;
    // we walk backward in the idxVarTree from the nearest dimension to the parent-most dimension
    Q.push_front(this);
    covered.insert(this);
    while( !Q.empty() )
    {
        for( const auto& dim : Q.front()->getDimensions() )
        {
            dimensions.insert(dim);
        }
        for( const auto& p : Q.front()->getParents() )
        {
            if( !covered.contains(p.get()) )
            {
                Q.push_back(p.get());
                covered.insert(p.get());
            }
        }
        Q.pop_front();
    }
    return (int)dimensions.size()-1; // we subtract one because the first position starts at 0
}

set<shared_ptr<IndexVariable>> Cyclebite::Grammar::getIndexVariables(const shared_ptr<Task>& t, const set<shared_ptr<InductionVariable>>& vars)
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
            for( const auto& i : b->getInstructions() )
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
    // the key needs to be a pair - the instruction may hold many idxVars and the value may be used all over hell (if it is a constant)
    // then the idxVar maps to a binary op that transforms a dimension, the instruction-value pair are identical pointers
    map<pair<const llvm::Instruction*, const llvm::Value*>, shared_ptr<IndexVariable>> nodeToIdx;
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
            vector<pair<pair<const llvm::Instruction*, const llvm::Value*>, AffineOffset>> indVarCandidates;
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
                        indVarCandidates.push_back( pair( pair(gep->getInst(), con), of) );
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
                    indVarCandidates.push_back(pair( pair(bin, bin), of));
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
                            indVarCandidates.back().second = of;
                        }
                        else
                        {
                            of.constant = 0; // undeterminable
                            indVarCandidates.back().second = of;
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
                        Cyclebite::Util::PrintVal(phi);
                        spdlog::warn("Could not figure out exactly what the offset for this phi should be, setting to + 1");
                        of.constant = 1;
                        of.transform = Cyclebite::Graph::Operation::add;
                    }
                    indVarCandidates.push_back( pair( pair(phi, phi), of) );
                }
                else if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
                {
                    // llvm front-end can do weird things in the new versions, like load from a multi-star pointer many times to get down to a more elementary array element
                    // e.g., if I have float a[x][y][z] aka float***, then the LLVM front end will get to float* by doing: float** b = load a, float* c = load b
                    // thus we need to walk through loads now - push the pointer operand into the q
                    if( const auto& ptrInst = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
                    {
                        if( !covered.contains(ptrInst) )
                        {
                            Q.push_back(ptrInst);
                            covered.insert(ptrInst);
                        }
                    } 
                }
                Q.pop_front();
            }
            // next, we build out all idxVars that can be discovered from the indices of this gep
            deque<shared_ptr<IndexVariable>> idxVarOrder;
            if( indVarCandidates.empty() ) 
            {
                // shouldn't happen anymore
                throw CyclebiteException("Could not get any idxVar candidates from a gep!");
                /*
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
                                Cyclebite::Util::PrintVal(idx->getNode()->getVal());
                            }
                            for( const auto& gep : gh )
                            {
                                Cyclebite::Util::PrintVal(gep->getInst());
                            }
                            Cyclebite::Util::PrintVal(gep->getInst());
                            throw CyclebiteException("Could not find parent idxVar!");
                        }
                        p->addChild(newIdx);
                        newIdx->addParent(p);
                    }
                }
                idxVarOrder.push_back( newIdx );*/
            }
            else if( indVarCandidates.size() == 1 )
            {
                shared_ptr<IndexVariable> newIdx = nullptr;
                if( nodeToIdx.contains( indVarCandidates.front().first ) )
                {
                    newIdx = nodeToIdx.at( indVarCandidates.front().first );
                }
                else
                {
                    if( const auto& inst = dynamic_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(indVarCandidates.front().first.second)) )
                    {
                        newIdx = make_shared<IndexVariable>( inst, inst );
                    }
                    else
                    {
                        newIdx = make_shared<IndexVariable>( Cyclebite::Graph::DNIDMap.at(indVarCandidates.front().first.second), gep );
                    }
                    nodeToIdx[ indVarCandidates.front().first ] = newIdx;
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
                            newIdx->addParent(p);
                        }
                    }
                }
                idxVarOrder.push_back(newIdx);
            }
            else
            {
                // for each candidate we encountered, it may or may not warrant an indexVariable
                // cases:
                // 1. binary op multiply: this undoubtedly requires an indexVariable, because it makes an affine transformation on the index space
                // 2. binary op add: warrants an index variable
                // 3. binary op or (in the case of optimizer loop unrolling): does not warrant an index variable
                for( auto bin = indVarCandidates.rbegin(); bin <= prev(indVarCandidates.rend()); bin++ )
                {
                    if( bin == indVarCandidates.rbegin() )
                    {
                        shared_ptr<IndexVariable> newIdx = nullptr;
                        if( nodeToIdx.contains( bin->first ) )
                        {
                            newIdx = nodeToIdx.at( bin->first );
                        }
                        else
                        {
                            if( const auto& inst = dynamic_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(bin->first.second)) )
                            {
                                newIdx = make_shared<IndexVariable>( inst, inst );
                            }
                            else
                            {
                                newIdx = make_shared<IndexVariable>( Cyclebite::Graph::DNIDMap.at(bin->first.second), gep );
                            }
                            nodeToIdx[ bin->first ] = newIdx;
                        }
                        shared_ptr<IndexVariable> child = nullptr;
                        if( nodeToIdx.contains( next(bin)->first) )
                        {
                            child = nodeToIdx.at( next(bin)->first );
                        }
                        else
                        {
                            if( const auto& inst = dynamic_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at( next(bin)->first.second)) )
                            {
                                child = make_shared<IndexVariable>( inst, inst, newIdx );
                            }
                            else
                            {
                                child = make_shared<IndexVariable>( Cyclebite::Graph::DNIDMap.at( next(bin)->first.second), gep, newIdx );
                            }
                            nodeToIdx[ next(bin)->first ] = child;
                        }
                        newIdx->addChild(child);
                        child->addParent(newIdx);
                        idxVarOrder.push_back(newIdx);
                        idxVarOrder.push_back(child);
                        bin = next(bin);
                    }
                    else if( bin == prev(indVarCandidates.rend()) )
                    {
                        // case where there are an odd number of entries
                        // then just create the last idxVar and update the hierarchy
                        shared_ptr<IndexVariable> newIdx = nullptr;
                        if( nodeToIdx.contains( bin->first ) )
                        {
                            newIdx = nodeToIdx.at( bin->first );
                        }
                        else
                        {
                            if( const auto& inst = dynamic_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(bin->first.second)))
                            {
                                newIdx = make_shared<IndexVariable>( inst, inst, idxVarOrder.back() );
                            }
                            else
                            {
                                newIdx = make_shared<IndexVariable>( Cyclebite::Graph::DNIDMap.at(bin->first.second), gep, idxVarOrder.back() );
                            }
                            nodeToIdx[ bin->first ] = newIdx;
                        }
                        newIdx->addParent(idxVarOrder.back());
                        idxVarOrder.back()->addChild(newIdx);
                        idxVarOrder.push_back(newIdx);
                        // don't increment the iterator, the loop will do that for us
                    }
                    else if( bin >= indVarCandidates.rend() )
                    {
                        // we are done
                    }
                    else
                    {
                        // default case
                        shared_ptr<IndexVariable> newIdx = nullptr;
                        if( nodeToIdx.contains( bin->first ) )
                        {
                            newIdx = nodeToIdx.at( bin->first );
                        }
                        else
                        {
                            if( const auto& inst = dynamic_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(bin->first.second)))
                            {
                                newIdx = make_shared<IndexVariable>( inst, inst, idxVarOrder.back() );
                            }
                            else
                            {
                                newIdx = make_shared<IndexVariable>( Cyclebite::Graph::DNIDMap.at(bin->first.second), gep, idxVarOrder.back() );
                            }
                            nodeToIdx[ bin->first ] = newIdx;
                        }
                        shared_ptr<IndexVariable> child = nullptr;
                        if( nodeToIdx.contains( next(bin)->first ) )
                        {
                            child = nodeToIdx.at( next(bin)->first );
                        }
                        else
                        {
                            if( const auto& inst = dynamic_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at( next(bin)->first.second)) )
                            {
                                child = make_shared<IndexVariable>( inst, inst, newIdx );
                            }
                            else
                            {
                                child = make_shared<IndexVariable>( Cyclebite::Graph::DNIDMap.at( next(bin)->first.second), gep, newIdx );
                            }
                            nodeToIdx [ next(bin)->first ] = newIdx;
                        }
                        newIdx->addChild(child);
                        idxVarOrder.back()->addChild(newIdx);
                        child->addParent(newIdx);
                        newIdx->addParent(idxVarOrder.back());
                        idxVarOrder.push_back(newIdx);
                        idxVarOrder.push_back(child);
                        bin = next(bin);
                    }
                }
            }
            // after the gep indices are done, we investigate the pointer operand of the gep
            // when geps are discovered in the gep pointer, those geps become parents of the gep indices
            // - think of the pointer gep as a higher-dimensional offset. It is offsetting the highest-dimension index in the current gep.
            // Thus, we need to draw the edges between the geps in pointer, and the highest-dimension index
            // first step: find all the geps that lead into the pointer operand of the current gep
            const llvm::GetElementPtrInst* parentGep = nullptr;
            deque<const llvm::Instruction*> instQ;
            set<const llvm::Instruction*> instCovered;
            if( const auto& inst = llvm::dyn_cast<llvm::Instruction>( llvm::cast<llvm::GetElementPtrInst>(gep->getInst())->getPointerOperand()) )
            {
                instQ.push_front(inst);
                instCovered.insert(inst);
            }
            while( !instQ.empty() )
            {
                if( const auto& gep = llvm::dyn_cast<llvm::GetElementPtrInst>(instQ.front()) )
                {
                    // if the gep under investigation maps to a parent through its pointer, that parent should already exist (since we evaluate geps in parent-to-child order)
                    parentGep = gep;
                    break;
                }
                else
                {
                    for( const auto& op : instQ.front()->operands() )
                    {
                        if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(op) )
                        {
                            if( !instCovered.contains(inst) )
                            {
                                instQ.push_back(inst);
                                instCovered.insert(inst);
                            }
                        }
                    }
                }
                instQ.pop_front();
            }
            if( parentGep )
            {
                // now we need the idxVar hierarchy that comes from the "right side" of the gep (the indices)
                // we just found this above... it is encoded in the idxVarOrder
                // thus we just add the new parent-most gep to the idxVarOrder tree
                // parentGep is used in the pointer operand of gep
                // thus, any idxVars associated with parentGep are parents of the gep under investigation
                // the child-most idxVar of parentGep is the parent of the parent-most idxVar of the gep under investigation
                set<shared_ptr<IndexVariable>, idxVarHierarchySort> parentIdxVars;
                for( const auto& idx : idxVars )
                {
                    if( idx->getInst()->getInst() == parentGep )
                    {
                        parentIdxVars.insert(idx);
                    }
                }
                // we connect the child-most parent idxVar to the parent-most var in the newly-minted idxVarOrder
                if( !parentIdxVars.empty() )
                {
                    vector<shared_ptr<IndexVariable>> sortedParentIdxVars(parentIdxVars.begin(), parentIdxVars.end());
                    sortedParentIdxVars.back()->addChild(idxVarOrder.front());
                    idxVarOrder.front()->addParent(sortedParentIdxVars.back());
                }
            }

            // finally, add all the new idxVars to the set
            for( const auto& idx : idxVarOrder )
            {
                idxVars.insert(idx);
            }
        }
    }
    for( const auto& idx : idxVars )
    {
        // find out if this index var is using an induction variable;
        for( const auto& iv : vars )
        {
            if( iv->isOffset(idx->getNode()->getVal()) )
            {
                idx->addDimension(iv);
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