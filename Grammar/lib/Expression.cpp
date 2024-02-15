//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Expression.h"
#include "Reduction.h"
#include "BasePointer.h"
#include "ConstantSymbol.h"
#include "FunctionExpression.h"
#include "Task.h"
#include "TaskParameter.h"
#include "OperatorExpression.h"
#include "Graph/inc/Dijkstra.h"
#include "Graph/inc/IO.h"
#include "Util/Exceptions.h"
#include "Util/Print.h"
#include "llvm/IR/Constants.h"
#include <iostream>

using namespace std;
using namespace Cyclebite::Grammar;

bool Expression::printedName = false;

void Expression::FindInputs( Expression* expr )
{
    // lets find all our inputs
    // symbols are hierarchically grouped, thus we need to search under the input list to find them all
    deque<shared_ptr<Symbol>> Q;
    set<shared_ptr<Symbol>> covered;
    if( const auto& opExpr = dynamic_cast<Cyclebite::Grammar::OperatorExpression*>(expr) )
    {
        for( const auto& ar : opExpr->getArgs() )
        {
            Q.push_front(ar);
            covered.insert(ar);
        }
    }
    else
    {
        for( const auto& s : expr->getSymbols() )
        {
            Q.push_front(s);
            covered.insert(s);
        }
    }
    while( !Q.empty() )
    {
        if( const auto& coll = dynamic_pointer_cast<Collection>(Q.front()) )
        {
            // collections present in the expression are always inputs
            expr->inputs.insert(coll);
        }
        else if( const auto& opExpr = dynamic_pointer_cast<OperatorExpression>(Q.front()) )
        {
            for( const auto& child : opExpr->getArgs() )
            {
                if( !covered.contains(child) )
                {
                    Q.push_back(child);
                    covered.insert(child);
                }
            }
        }
        else if( const auto& expr = dynamic_pointer_cast<Expression>(Q.front()) )
        {
            for( const auto& child : expr->getSymbols() )
            {
                if( !covered.contains(child) )
                {
                    Q.push_back(child);
                    covered.insert(child);
                }
            }
        }
        Q.pop_front();
    }
}

Expression::Expression( const std::shared_ptr<Task>& ta, const vector<shared_ptr<Symbol>>& in, const vector<Cyclebite::Graph::Operation>& o, const shared_ptr<Symbol>& out, const string name ) : Symbol(name), t(ta), output(out), ops(o), symbols(in)
{
    if( in.empty() && o.empty() )
    {
        throw CyclebiteException("Expression cannot be empty!");
    }
    printedName = false;
    FindInputs(this);
} 

const shared_ptr<Task>& Expression::getTask() const
{
    return t;
}

string Expression::dump() const
{
    string expr = "";
    bool flip = false;
    if( !printedName )
    {
        flip = true;
        if( output )
        {
            expr += output->dump() + " <- ";
        }
        expr += name + " = ";
    }
    printedName = true;
    if( !symbols.empty() )
    {
        auto b = symbols.begin();
        auto o = ops.begin();
        expr += " "+(*b)->dump();
        b = next(b);
        while( b != symbols.end() )
        {
            expr += " "+string(Cyclebite::Graph::OperationToString.at(*o))+" "+(*b)->dump();
            b = next(b);
            o = next(o);
        }
    }
    printedName = flip ? !printedName : printedName;
    return expr;
}

string Expression::dumpHalide( const map<shared_ptr<Symbol>, shared_ptr<Symbol>>& symbol2Symbol ) const
{
    string expr = "";
    if( !symbols.empty() )
    {
        auto b = symbols.begin();
        auto o = ops.begin();
        expr += " "+(*b)->dumpHalide(symbol2Symbol);
        b = next(b);
        while( b != symbols.end() )
        {
            expr += " "+string(Cyclebite::Graph::OperationToString.at(*o))+" "+(*b)->dumpHalide(symbol2Symbol);
            b = next(b);
            o = next(o);
        }
    }
    return expr;
}

string Expression::dumpHalideReference( const map<shared_ptr<Symbol>, shared_ptr<Symbol>>& symbol2Symbol ) const
{
    vector<shared_ptr<InductionVariable>> exprDims;
    // 5b.2 enumerate all vars used in the expression
    if( const auto& outputColl = dynamic_pointer_cast<Collection>(output) )
    {
        for( const auto& dim : outputColl->getDimensions() )
        {
            if( const auto& iv = dynamic_pointer_cast<InductionVariable>(dim) )
            {
                exprDims.push_back(iv);
            }
        }
    }
    else
    {
        throw CyclebiteException("Cannot print a task that doesn't have a collection as output!");
    }
    string ref = name+"(";
    if( exprDims.size() )
    {
        ref += exprDims.front()->dumpHalide(symbol2Symbol);
        for( auto it = next(exprDims.begin()); it != exprDims.end(); it++ )
        {
            ref += ", "+(*it)->dumpHalide(symbol2Symbol);
        }
    }
    ref += ") ";
    return ref;
}

const vector<shared_ptr<Symbol>>& Expression::getSymbols() const
{
    return symbols;
}

const set<shared_ptr<ReductionVariable>> Expression::getRVs() const
{
    set<shared_ptr<ReductionVariable>> RVs;
    deque<shared_ptr<Symbol>> Q;
    set<shared_ptr<Symbol>> covered;
    if( const auto& op = dynamic_cast<const OperatorExpression*>(this) )
    {
        for( const auto& arg : op->getArgs() )
        {
            Q.push_front(arg);
            covered.insert(arg);
        }
    }
    else if( const auto& expr = dynamic_cast<const Expression*>(this) )
    {
        for( const auto& arg : symbols )
        {
            Q.push_front(arg);
            covered.insert(arg);
        }
    }
    else
    {
        return RVs;
    }
    while( !Q.empty() )
    {
        if( const auto& rv = dynamic_pointer_cast<ReductionVariable>(Q.front()) )
        {
            RVs.insert(rv);
            covered.insert(rv);
        }
        else if( const auto& opExpr = dynamic_pointer_cast<OperatorExpression>(Q.front()) )
        {
            for( const auto& arg : opExpr->getArgs() )
            {
                if( !covered.contains(arg) )
                {
                    Q.push_back(arg);
                    covered.insert(arg);
                }
            }
        }
        else if( const auto& red = dynamic_pointer_cast<Reduction>(Q.front()) )
        {
            RVs.insert(red->getRVs().begin(), red->getRVs().end());
            for( const auto& sym : red->getSymbols() )
            {
                if( !covered.contains(sym) )
                {
                    Q.push_back(sym);
                    covered.insert(sym);
                }
            }
        }
        else if( const auto& expr = dynamic_pointer_cast<Expression>(Q.front()) )
        {
            for( const auto& sym : expr->getSymbols() )
            {
                if( !covered.contains(sym) )
                {
                    Q.push_back(sym);
                    covered.insert(sym);
                }
            }
        }
        Q.pop_front();
    }
    return RVs;
}

bool Expression::hasParallelReduction() const
{
    for( const auto& rv : getRVs() )
    {
        if( const auto& coll = dynamic_pointer_cast<Collection>(output) )
        {
            for( const auto& var : coll->getIndices() )
            {
                for( const auto& varDim : var->getDimensions() )
                {
                    if( rv->getDimensions().contains(varDim) )
                    {
                        // this store touches the same reduction as the reduction variable, indicating that unique values within the reduction are stored
                        // this is not a parallel reduction
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

const vector<Cyclebite::Graph::Operation>& Expression::getOps() const
{
    return ops;
}

const set<shared_ptr<Collection>> Expression::getCollections() const
{
    set<shared_ptr<Collection>> collections;
    for( const auto& i : inputs )
    {
        if( const auto& coll = dynamic_pointer_cast<Collection>(i) )
        {
            collections.insert(coll);
        }
    }
    return collections;
}

const set<shared_ptr<Symbol>>& Expression::getInputs() const
{
    return inputs;
}

const shared_ptr<Symbol>& Expression::getOutput() const
{
    return output;
}

/// @brief Recursively builds an expression out of an LLVM instruction
/// @param node The node of the highest-level instruction of this expression. This argument is simply used to get access to the original llvm::Module of the instruction
/// @param t The task in which this expression should belong
/// @param op The llvm::Value to build an expression for. This op may or may not belong to "node" (it may belong to a predecessor of "node")
/// @param nodeToExpr A map from DataValue to Symbol. Updated every time this method creates a new symbol
/// @param colls The collections that feed into this task. They are used (when possible) to represent loads and stores in the expressions that are built
/// @return A vector of symbols that represents the op argument that is passed. When op is a vector, this vector will have more than one entry. Single entry otherwise.
vector<shared_ptr<Symbol>> buildExpression( const shared_ptr<Cyclebite::Graph::Inst>& node,
                                            const shared_ptr<Task>& t,  
                                            const llvm::Value* op, 
                                            map<shared_ptr<Cyclebite::Graph::DataValue>, shared_ptr<Symbol>>& nodeToExpr, 
                                            const set<shared_ptr<Collection>>& colls,
                                            const set<shared_ptr<InductionVariable>>& vars )
{
    // This vector holds the symbols we generate for this object
    vector<shared_ptr<Symbol>> newSymbols;
    if( const auto inst = llvm::dyn_cast<llvm::Instruction>(op) )
    {
        const auto opInst = static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(inst));
        // this parameter maps to the place this object is stored
        // (see Cyclebite::Grammar::Expression::getStored() documentation for more on this)
        shared_ptr<Symbol> symbolOutput = nullptr;
        auto succs = opInst->getSuccessors();
        for( const auto& succ : opInst->getSuccessors() )
        {
            if( const auto& succInst = dynamic_pointer_cast<Cyclebite::Graph::Inst>(succ->getSnk()) )
            {
                if( succInst->getOp() == Cyclebite::Graph::Operation::store )
                {
                    if( !nodeToExpr.contains(succInst) )
                    {
                        Cyclebite::Util::PrintVal(succInst->getInst());
                        throw CyclebiteException("Could not map an expression's store to a Symbol!");
                    }
                    symbolOutput = nodeToExpr.at(succInst);
                    break;
                }
            }
        }
        if( nodeToExpr.find(opInst) != nodeToExpr.end() )
        {
            // this value comes from a previous function group operator, thus it should be a symbol in the expression
            newSymbols.push_back(nodeToExpr.at(opInst));
        }
        else if( !t->find(opInst) )
        {
            // this is an out-of-task expression
            // make a placeholder and deal with it later
            auto newSymbol = make_shared<TaskParameter>(opInst, t);
            nodeToExpr[ opInst ] = newSymbol;
            newSymbols.push_back(newSymbol);
        }
        else if( const auto ld = llvm::dyn_cast<llvm::LoadInst>(inst) )
        {
            // this value feeds the function group
            // what we need to do is map the load to the collection that represents the value it loads
            // this collection will become the symbol in the expression
            shared_ptr<Collection> found = nullptr;
            for( const auto& coll : colls )
            {
                // collections are distinguished by their indexVariables, not necessarily their base pointer
                // in order to find the right one, we need to figure out which indexVariable is used in this load
                if( coll->getLoad() == ld )
                {
#ifdef DEBUG
                    if( found )
                    {
                        cout << endl;
                        Cyclebite::Util::PrintVal(ld);
                        Cyclebite::Util::PrintVal(coll->getBP()->getNode()->getVal());
                        Cyclebite::Util::PrintVal(coll->getLoad());
                        Cyclebite::Util::PrintVal(coll->getIndices().back()->getNode()->getInst());
                        Cyclebite::Util::PrintVal(found->getBP()->getNode()->getVal());
                        Cyclebite::Util::PrintVal(found->getLoad());
                        Cyclebite::Util::PrintVal(found->getIndices().back()->getNode()->getInst());
                        throw CyclebiteException("Mapped more than one collection to a load value!");
                    }
                    else
                    {
                        found = coll;
                    }
#else
                    found = coll;
                    break;
#endif
                }
            }
            if( found )
            {
                newSymbols.push_back(found);
            }
            else if( const auto& con = llvm::dyn_cast<llvm::Constant>( getPointerSource(ld->getPointerOperand()) ) )
            {
                // this should've been handled by the getConstants() method in Process()... so we'll just go through the motions for now (BW 2024-02-14)
                // in that case we are interested in finding out which value we are pulling from the structure
                // this may or may not be possible, if the indices are or aren't statically determinable
                // ex: StencilChain/Naive(BB 170)
                if( Cyclebite::Graph::DNIDMap.contains(con) )
                {
                    newSymbols.push_back( nodeToExpr.at(Cyclebite::Graph::DNIDMap.at(con)) );
                }
                else if( con->getType()->isPointerTy() )
                {
                    bool canBeNull = false;
                    bool canBeFreed = false;
                    if( con->getPointerDereferenceableBytes(node->getInst()->getParent()->getParent()->getParent()->getDataLayout(), canBeNull, canBeFreed) < ALLOC_THRESHOLD )
                    {
                        // the pointer's allocation is not large enough, thus there is no collection that will represent it
                        // we still need this value in our expression, whatever it may be, so just make a constant symbol for it
                        int constantVal = 0;
                        auto newSymbol = make_shared<ConstantSymbol>(con, &constantVal, ConstantType::INT);
                        nodeToExpr[ opInst ] = newSymbol;
                        newSymbols.push_back(newSymbol); 
                    }
                }
            }
            else 
            {
                Cyclebite::Util::PrintVal(ld);
                for( const auto& coll : colls )
                {
                    Cyclebite::Util::PrintVal(coll->getBP()->getNode()->getVal());
                    for( const auto& idx : coll->getIndices() )
                    {
                        Cyclebite::Util::PrintVal(idx->getNode()->getInst());
                    }
                    if( coll->getLoad() )
                    {
                        Cyclebite::Util::PrintVal(coll->getLoad());
                    }
                }
                throw CyclebiteException("Could not find a collection to describe this load!");
            }
        }
        else if( const auto st = llvm::dyn_cast<llvm::StoreInst>(inst) )
        {
            // this instruction stores the function group result, do nothing for now
        }
        else if( const auto bin = llvm::dyn_cast<llvm::BinaryOperator>(inst) )
        {
            // binary operators can be embedded inside other expressions in the function group
            // e.g., WAMI/debayer Task 18 (BBID)
            // this operator needs an entire expression built for it, so we call the expression builder
            vector<shared_ptr<Symbol>> args;
            for( const auto& binOp : bin->operands() )
            {
                for( const auto& news : buildExpression(opInst, t, binOp, nodeToExpr, colls, vars) )
                {
                    args.push_back( news );
                }
            }
            vector<Cyclebite::Graph::Operation> binOps;
            binOps.push_back( Cyclebite::Graph::GetOp(bin->getOpcode()) );
            auto binExpr = make_shared<Expression>( t, args, binOps, symbolOutput );
            nodeToExpr[ opInst ] = binExpr;
            newSymbols.push_back(binExpr);
        }
        else if( const auto& phi = llvm::dyn_cast<llvm::PHINode>(op) )
        {
            // phis imply a loop-loop dependence or predication
            // in order to know that we need a predication expression, more than one of the incoming values must be live
            set<const llvm::Value*> liveIncomingValues;
            for( unsigned i = 0; i < phi->getNumIncomingValues(); i++ )
            {
                auto incomingBlock = phi->getIncomingBlock(i);
                if( Cyclebite::Graph::BBCBMap.contains(incomingBlock) )
                {
                    // if the predecessor block is live, see if the edge between that block and this one is live
                    auto predBlock = Cyclebite::Graph::BBCBMap.at(incomingBlock);
                    auto succBlock = Cyclebite::Graph::BBCBMap.at(phi->getParent());
                    if( (predBlock->isPredecessor(succBlock) != nullptr) && (succBlock->isSuccessor(predBlock) != nullptr) )
                    {
                        // it is live, put the value in the live set
                        // FIX: to get temporal mitigation gemm and PERFECT/2DConv to work, we don't consider constants
                        if( !llvm::isa<llvm::Constant>(phi->getIncomingValue(i)) )
                        {
                            liveIncomingValues.insert(phi->getIncomingValue(i));
                        }
                    }
                }
            }
            if( liveIncomingValues.size() > 1 )
            {
                Cyclebite::Util::PrintVal(phi);
                throw CyclebiteException("Cannot yet build predication expressions for this task!");
            }
            else if( liveIncomingValues.empty() )
            {
                Cyclebite::Util::PrintVal(phi);
                throw CyclebiteException("Could not find a live incoming value for this phi that is used in the task expression!");
            }
            else
            {
                // we just need to build out values for the lone live value
                vector<shared_ptr<Symbol>> args;
                for( const auto& news : buildExpression(node, t, *liveIncomingValues.begin(), nodeToExpr, colls, vars) )
                {
                    args.push_back( news );
                }
                vector<Cyclebite::Graph::Operation> binOps;
                auto binExpr = make_shared<Expression>( t, args, binOps, symbolOutput );
                nodeToExpr[ opInst ] = binExpr;
                newSymbols.push_back(binExpr);
            }
        }
        else if( const auto& sel = llvm::dyn_cast<llvm::SelectInst>(op) )
        {
            // if one side of the expression is a constant, we can just build the other side and ignore the condition
            const llvm::Constant* con = nullptr;
            const llvm::Value* other = nullptr;
            if( const auto& c = llvm::dyn_cast<llvm::Constant>(sel->getTrueValue()) )
            {
                con = c;
                other = sel->getFalseValue();
            }
            else if( const auto& c = llvm::dyn_cast<llvm::Constant>(sel->getFalseValue()) )
            {
                con = c;
                other = sel->getTrueValue();
            }
            if( con )
            {
                vector<shared_ptr<Symbol>> args;
                for( const auto& news : buildExpression(opInst, t, other, nodeToExpr, colls, vars) )
                {
                    args.push_back(news);
                }
                vector<Cyclebite::Graph::Operation> ops;
                auto newExpr = make_shared<Expression>( t, args, ops, symbolOutput );
                nodeToExpr[ opInst ] = newExpr;
                newSymbols.push_back(newExpr);
            }
        }
        else if( const auto& cast = llvm::dyn_cast<llvm::CastInst>(op) )
        {
            vector<shared_ptr<Symbol>> args;
            for( const auto& castOp : cast->operands() )
            {
                // call expression builder
                for( const auto& news : buildExpression(opInst, t, castOp, nodeToExpr, colls, vars) )
                {
                    args.push_back( news );
                }
            }
            auto castExpr = make_shared<OperatorExpression>( t, Cyclebite::Graph::GetOp(cast->getOpcode()), args, symbolOutput );
            nodeToExpr[ opInst ] = castExpr;
            newSymbols.push_back(castExpr);
        }
        else if( const auto& call = llvm::dyn_cast<llvm::CallBase>(op) )
        {
            vector<shared_ptr<Symbol>> args;
            for( const auto& argOp : llvm::cast<llvm::CallBase>(opInst->getInst())->args() )
            {
                // call expression builder
                for( const auto& news : buildExpression(opInst, t, argOp.get(), nodeToExpr, colls, vars) )
                {
                    if( Cyclebite::Graph::DNIDMap.contains(argOp.get()) )
                    {
                        nodeToExpr[ Cyclebite::Graph::DNIDMap.at(argOp.get()) ] = news;
                    }
                    args.push_back( news );
                }
            }
            const llvm::Function* f;
            if( llvm::cast<llvm::CallBase>(opInst->getInst())->getCalledFunction() == nullptr )
            {
                // we have to find it in our dynamic information
                for( const auto& succ : opInst->parent->getSuccessors() )
                {
                    auto targetParent = static_pointer_cast<Cyclebite::Graph::ControlNode>(succ->getSnk());
                    for( const auto& bb : Cyclebite::Graph::BBCBMap )
                    {
                        if( bb.second == targetParent )
                        {
                            f = bb.first->getParent();
                            // confirm this function is not the same as the caller function
                            // this will break under recursion
                            if( f != opInst->getInst()->getParent()->getParent() )
                            {
                                break;
                            }
                            else
                            {
                                // wrong function, must have searched the wrong opInst successor
                                f = nullptr;
                            }
                        }
                    }
                    if( f )
                    {
                        break;
                    }
                }
                if( !f )
                {
                    throw CyclebiteException("Could not determine the function of a functionexpression!");
                }
            }
            else
            {
                f = llvm::cast<llvm::CallBase>(opInst->getInst())->getCalledFunction();
            }
            auto funcExpr = make_shared<FunctionExpression>( t, f, args, symbolOutput );
            nodeToExpr[ opInst ] = funcExpr;
            newSymbols.push_back(funcExpr);
        }
        else if( const auto& extr = llvm::dyn_cast<llvm::ExtractValueInst>(op) )
        {
            // this shims a vector to, say, a function argument that must be a scalar
            // thus we just skip this instruction and go to the source of its vector
            for( const auto& news : buildExpression(node, t, extr->getAggregateOperand(), nodeToExpr, colls, vars) )
            {
                nodeToExpr[ opInst ] = news;
                newSymbols.push_back(news);
            }
        }
        else if( const auto& shuffle = llvm::dyn_cast<llvm::ShuffleVectorInst>(op) )
        {
            // shuffles appear to be useful for concatenating elemnents of different vectors into the same vector
            // the only example so far that has used this is optimized (-O3) StencilChain/Naive, which uses an "identity shuffle" (example, StencilChain/Naive)
            // we don't support shufflevectors yet because they usually contain more than one operation at once, and may be transformed later
            // example: StencilChain/Naive/DFG_Kernel15.svg (shufflevector transforms i8 to i32, then i32 is converted to float before the MAC takes place)
            Cyclebite::Util::PrintVal(node->getInst());
            throw CyclebiteException("Cannot support shufflevector instructions yet!");
        }
        else if( const auto& unary = llvm::dyn_cast<llvm::UnaryInstruction>(op) )
        {
            // fneg is the most common example of this
            vector<shared_ptr<Symbol>> args;
            for( const auto& unaryOp : unary->operands() )
            {
                for( const auto& news : buildExpression(opInst, t, unaryOp, nodeToExpr, colls, vars) )
                {
                    args.push_back( news );
                }
            }
            auto unaryExpr = make_shared<OperatorExpression>(t, Cyclebite::Graph::GetOp(unary->getOpcode()), args);
            nodeToExpr[ opInst ] = unaryExpr;
            newSymbols.push_back(unaryExpr);
        }
    }
    else if( auto con = llvm::dyn_cast<llvm::Constant>(op) )
    {
        if( con->getType()->isIntegerTy() )
        {
            newSymbols.push_back(make_shared<ConstantSymbol>( con, con->getUniqueInteger().getRawData(), ConstantType::INT64));
        }
        else if( con->getType()->isFloatTy() )
        {
            if( const auto& conF = llvm::dyn_cast<llvm::ConstantFP>(con) )
            {
                float val = conF->getValueAPF().convertToFloat();
                newSymbols.push_back(make_shared<ConstantSymbol>( con, &val, ConstantType::FLOAT));
            }
            else
            {
                throw CyclebiteException("Could not extract float from constant float!");
            }
        }
        else if( con->getType()->isDoubleTy() )
        {
            if( const auto& conD = llvm::dyn_cast<llvm::ConstantFP>(con) )
            {
                double val = conD->getValueAPF().convertToDouble();
                newSymbols.push_back(make_shared<ConstantSymbol>( con, &val, ConstantType::DOUBLE));
            }
            else
            {
                throw CyclebiteException("Could not extract double from constant double!");
            }
        }
        else if( const auto& undef = llvm::dyn_cast<llvm::UndefValue>(op) )
        {
            // the value is a constant that is not known till runtime
            // llvm has undefined values as a result of the optimizer to allow for constant-qualified values that aren't known till runtime
            // we don't support this yet
            Cyclebite::Util::PrintVal(con);
            Cyclebite::Util::PrintVal(node->getInst());
            throw CyclebiteException("Cannot support undefined constants yet");
        }
        else if( const auto& convec = llvm::dyn_cast<llvm::ConstantVector>(op) )
        {
            for( unsigned i = 0; i < convec->getNumOperands(); i++ )
            {
                if( convec->getOperand(i)->getType()->isIntegerTy() )
                {
                    newSymbols.push_back(make_shared<ConstantSymbol>( con, convec->getOperand(i)->getUniqueInteger().getRawData(), ConstantType::INT64));
                }
                else if( convec->getOperand(i)->getType()->isFloatTy() )
                {
                    if( const auto& conF = llvm::dyn_cast<llvm::ConstantFP>(convec->getOperand(i)) )
                    {
                        float val = conF->getValueAPF().convertToFloat();
                        newSymbols.push_back(make_shared<ConstantSymbol>( con, &val, ConstantType::FLOAT));
                    }
                    else
                    {
                        throw CyclebiteException("Could not extract float from constant float!");
                    }
                }
                else if( convec->getOperand(i)->getType()->isDoubleTy() )
                {
                    if( const auto& conD = llvm::dyn_cast<llvm::ConstantFP>(convec->getOperand(i)) )
                    {
                        double val = conD->getValueAPF().convertToDouble();
                        newSymbols.push_back(make_shared<ConstantSymbol>( con, &val, ConstantType::DOUBLE));
                    }
                    else
                    {
                        throw CyclebiteException("Could not extract double from constant double!");
                    }
                }
            }
        }
        else if( const auto& func = llvm::dyn_cast<llvm::Function>(con) )
        {
            // we have found a constant function call that returns a value, more generally a "global object" in the llvm api
            vector<shared_ptr<Symbol>> args;
            for( const auto& argOp : con->operands() )
            {
                if( argOp == func )
                {
                    continue;
                }
                // call expression builder
                for( const auto& news : buildExpression(node, t, argOp.get(), nodeToExpr, colls, vars) )
                {
                    args.push_back( news );
                }
            }
            auto newSymbol = make_shared<FunctionExpression>(t, func, args);
            newSymbols.push_back(newSymbol);
        }
        else
        {
            Cyclebite::Util::PrintVal(op);
            Cyclebite::Util::PrintVal(node->getVal());
            throw CyclebiteException("Constant used in an expression is not an integer!");
        }
    }
    else if( const auto& arg = llvm::dyn_cast<llvm::Argument>(op) )
    {
        // this value comes from somewhere else
        // assign a placeholder for it
        auto newSymbol = make_shared<TaskParameter>(Cyclebite::Graph::DNIDMap.at(arg), t);
        nodeToExpr[Cyclebite::Graph::DNIDMap.at(arg)] = newSymbol;
        newSymbols.push_back( newSymbol );
    }
    else
    {
        Cyclebite::Util::PrintVal(op);
        Cyclebite::Util::PrintVal(node->getVal());
        throw CyclebiteException("Cannot recognize this operand type when building an expression!");
    }
    if( newSymbols.empty() )
    {
        Cyclebite::Util::PrintVal(op);
        throw CyclebiteException("Could not build a symbol for this llvm::value!");
    }
    return newSymbols;
}

/// @brief Expression builder for a task
/// @param t        The task in which this function group belongs
/// @param insts    A vector of all instructions in the function group, in reverse-order that they operate (inst that gets stored is first, inst(s) with only memory inputs is last)
/// @param rvs      Reduction variables, if necessary. If this argument is non-empty, the returned expression is a Reduction. Otherwise it is an Expression.
/// @param colls    The collections in the task
/// @return         An expression that describes the entire function group. Member symbols may contain symbols within them.
const shared_ptr<Expression> constructExpression( const shared_ptr<Task>& t, 
                                                  const vector<shared_ptr<Cyclebite::Graph::Inst>>& insts, 
                                                  const set<shared_ptr<ReductionVariable>>& rvs, 
                                                  const set<shared_ptr<Collection>>& colls,
                                                  const set<shared_ptr<ConstantSymbol>>& cons,
                                                  const set<shared_ptr<InductionVariable>>& vars )
{
    shared_ptr<Expression> expr;

    map<shared_ptr<Cyclebite::Graph::DataValue>, shared_ptr<Symbol>> nodeToExpr;
    // there are a few DataValues that need to be mapped to their symbols before we start generating this expression
    // first, induction variables in case their integers are used in the expression for some weird reason
    for( const auto& var : vars )
    {
        nodeToExpr[ var->getNode() ] = var;
    }
    // second, the loads and stores of collections (these will be used to find the input and output values of the expression)
    for( const auto& coll : colls )
    {
        if( coll->getLoad() )
        {
            nodeToExpr[ Cyclebite::Graph::DNIDMap.at(coll->getLoad()) ] = coll;
        }
        for( const auto& st : coll->getStores() )
        {
            nodeToExpr[ Cyclebite::Graph::DNIDMap.at(st) ] = coll;
        }
    }
    // initiate the constants that were found before in the nodeToExpr array
    for( const auto& con : cons )
    {
        // the DNIDMap will only contain constants that come from GlobalVariables - these are the objects that may contain static arrays (see Graph/IO.cpp lines 2007-2020)
        if( Cyclebite::Graph::DNIDMap.contains(con->getConstant()) )
        {
            nodeToExpr[ Cyclebite::Graph::DNIDMap.at(con->getConstant()) ] = con;
        }
    }
    // if there is a reduction variable, it's phi should be put into nodeToExpr
    // but not the rv's node - the node is a binary operator that carries out the reduction (it is accounted for in the reduction expression)
    if( !rvs.empty() )
    {
        for( const auto& rv : rvs )
        {
            for( const auto& addr : rv->getAddresses() )
            {
                nodeToExpr[ addr ] = rv;
            }
            if( const auto& inst = dynamic_pointer_cast<Cyclebite::Graph::Inst>(rv->getNode()) )
            {
                auto symbols = buildExpression(inst, t, rv->getNode()->getVal(), nodeToExpr, colls, vars);
                if( symbols.size() != 1 )
                {
                    throw CyclebiteException("Cannot yet handle the case where a reduction variable node generates more than one symbol!");
                }
                nodeToExpr[ rv->getNode() ] = *symbols.begin();
            }
            else
            {
                throw CyclebiteException("Cannot yet handle the case where a reduction variable node is not a Graph::Inst!");
            }
            //nodeToExpr[rv->getNode()] = rv;
        }
    }

    // now we iterate (from start to finish) over the instructions in the expression, each time building a Symbol for each one, until all instructions in the expression have a symbol built for them
    vector<Cyclebite::Graph::Operation> ops;
    auto builtExpr = buildExpression( *insts.begin(), t, (*insts.begin())->getInst(), nodeToExpr, colls, vars );
    /*if( rv )
    {
        if( builtExpr.empty() )
        {
            vector<shared_ptr<Symbol>> vec;
            // we have a "counter" expression, where we just load something, add to it, then store it back
            // thus, there should be a constant in the add. Find it, put it into the vec list, and construct a reduction on it
            if( const auto& inst = dynamic_pointer_cast<Cyclebite::Graph::Inst>(rv->getNode()) )
            {
                for( const auto& op : inst->getInst()->operands() )
                {
                    if( const auto& con = llvm::dyn_cast<llvm::Constant>(op) )
                    {
                        if( con->getType()->isIntegerTy() )
                        {
                            vec.push_back( make_shared<ConstantSymbol<int64_t>>( *con->getUniqueInteger().getRawData()) );
                        }
                        else if( con->getType()->isFloatTy() )
                        {
                            if( const auto& conF = llvm::dyn_cast<llvm::ConstantFP>(con) )
                            {
                                vec.push_back( make_shared<ConstantSymbol<float>>( conF->getValueAPF().convertToFloat() ));
                            }
                            else
                            {
                                throw CyclebiteException("Could not extract float from constant float!");
                            }
                        }
                        else if( con->getType()->isDoubleTy() )
                        {
                            if( const auto& conD = llvm::dyn_cast<llvm::ConstantFP>(con) )
                            {
                                vec.push_back( make_shared<ConstantSymbol<double>>( conD->getValueAPF().convertToDouble()) );
                            }
                            else
                            {
                                throw CyclebiteException("Could not extract double from constant double!");
                            }
                        }
                    }
                }
            }
            expr = make_shared<Reduction>(t, rv, vec, ops);
        }
        else
        {
            // wrap the constructed expr in a reduction
            vector<Cyclebite::Graph::Operation> redOp;
            auto red = make_shared<Reduction>(t, rv, builtExpr, redOp, static_pointer_cast<Expression>((*builtExpr.rbegin()))->getOutput() );
            nodeToExpr[rv->getNode()] = red;
            expr = red;
        }
    }
    else
    {
        expr = static_pointer_cast<Expression>(*builtExpr.rbegin());
    }*/
    expr = static_pointer_cast<Expression>(*builtExpr.begin());
    return expr;
}

vector<shared_ptr<Expression>> Cyclebite::Grammar::getExpressions( const shared_ptr<Task>& t, 
                                                                   const set<shared_ptr<Collection>>& colls, 
                                                                   const set<shared_ptr<ReductionVariable>>& rvs, 
                                                                   const set<shared_ptr<ConstantSymbol>>& cons,
                                                                   const set<shared_ptr<InductionVariable>>& vars )
{
    vector<shared_ptr<Expression>> FGTs;
    // step 1: gather all store operations that get their value from the function group
    // these stores are the number of fine-grained tasks in the CGT
    set<shared_ptr<Cyclebite::Graph::Inst>, Cyclebite::Graph::p_GNCompare> functionStores;
    for( const auto& c : t->getCycles() )
    {
        for( const auto& block : c->getBody() )
        {
            for( const auto& node : block->getInstructions() )
            {
                if( node->getOp() == Graph::Operation::store )
                {
                    // get the value operand - if it is in the function group we remember this store
                    if( Graph::DNIDMap.contains( llvm::cast<llvm::StoreInst>(node->getInst())->getValueOperand() ) )
                    {
                        if( const auto valueInst = dynamic_pointer_cast<Graph::Inst>( Graph::DNIDMap.at(llvm::cast<llvm::StoreInst>(node->getInst())->getValueOperand()) ) )
                        {
                            if( valueInst->isFunction() )
                            {
                                functionStores.insert(node);
                            }
                        }
                    }
                }
            }
        }
    }
    if( functionStores.empty() )
    {
        throw CyclebiteException("Function group is empty!");
    }
    // step2: for each store, walk backward through the function group until the function group runs out
    // function groups are sorted in reverse-order (the last instruction in the sequence is stored first)
    set<vector<shared_ptr<Graph::Inst>>> functionGroups;
    for( const auto& storeNode : functionStores )
    {
        deque<shared_ptr<Cyclebite::Graph::Inst>> Q;
        set<shared_ptr<Cyclebite::Graph::DataValue>, Cyclebite::Graph::p_GNCompare> covered;
        vector<shared_ptr<Graph::Inst>> group;
        Q.push_front( static_pointer_cast<Graph::Inst>( Graph::DNIDMap.at(llvm::cast<llvm::StoreInst>(storeNode->getInst())->getValueOperand()) ) );
        covered.insert(Graph::DNIDMap.at(llvm::cast<llvm::StoreInst>(storeNode->getInst())->getValueOperand()));
        group.push_back(static_pointer_cast<Graph::Inst>( Graph::DNIDMap.at(llvm::cast<llvm::StoreInst>(storeNode->getInst())->getValueOperand()) ));
        while( !Q.empty() )
        {
            for( const auto& pred : Q.front()->getPredecessors() )
            {
                if( const auto& predInst = dynamic_pointer_cast<Graph::Inst>(pred->getSrc()) )
                {
                    if( predInst->isFunction() )
                    {
                        if( !covered.contains(predInst) )
                        {
                            group.push_back(predInst);
                            Q.push_back(predInst);
                            covered.insert(predInst);
                        }
                    }
                }
            }
            Q.pop_front();
        }
        FGTs.push_back( constructExpression(t, group, rvs, colls, cons, vars) );
    }
    return FGTs;
}