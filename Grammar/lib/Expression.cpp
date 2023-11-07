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

Expression::Expression( const std::shared_ptr<Task>& ta, const vector<shared_ptr<Symbol>>& in, const vector<Cyclebite::Graph::Operation>& o, const shared_ptr<Symbol>& out, const string name ) : Symbol(name), t(ta), output(out), ops(o), symbols(in)
{
    if( in.empty() && o.empty() )
    {
        throw CyclebiteException("Expression cannot be empty!");
    }
    printedName = false;
    // lets find all our inputs
    // symbols are hierarchically grouped, thus we need to search under the input list to find them all
    deque<shared_ptr<Symbol>> Q;
    set<shared_ptr<Symbol>> covered;
    for( const auto& sym : in )
    {
        Q.push_front(sym);
        covered.insert(sym);
    }
    while( !Q.empty() )
    {
        if( const auto& coll = dynamic_pointer_cast<Collection>(Q.front()) )
        {
            // collections present in the expression are always inputs
            inputs.insert(coll);
        }
        if( const auto& expr = dynamic_pointer_cast<Expression>(Q.front()) )
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
    /*else if( o.size() != s.size()-1 )
    {
        for( const auto& op : o )
        {
            cout << Graph::OperationToString.at(op) << endl;
        }
        for( const auto& sym : s )
        {
            sym->dump();
        }
        throw CyclebiteException("There should be "+to_string(symbols.size()-1)+" operations for an expression with "+to_string(symbols.size())+" symbols! Operation count: "+to_string(o.size()));
    }*/
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

const vector<shared_ptr<Symbol>>& Expression::getSymbols() const
{
    return symbols;
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
/// @param node The node of the highest-level instruction of this expression. This argument is simply used to get access to the original LLVM::Module of the instruction
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
    vector<shared_ptr<Symbol>> newSymbols;
    if( const auto inst = llvm::dyn_cast<llvm::Instruction>(op) )
    {
        const auto opNode = Cyclebite::Graph::DNIDMap.at(inst);
        if( nodeToExpr.find(opNode) != nodeToExpr.end() )
        {
            // this value comes from a previous function group operator, thus it should be a symbol in the expression
            newSymbols.push_back(nodeToExpr.at(opNode));
        }
        else if( !t->find(opNode) )
        {
            // this is an out-of-task expression
            // make a placeholder and deal with it later
            auto newSymbol = make_shared<TaskParameter>(opNode, t);
            nodeToExpr[ opNode ] = newSymbol;
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
                        PrintVal(ld);
                        PrintVal(coll->getBP()->getNode()->getVal());
                        PrintVal(coll->getLoad());
                        PrintVal(coll->getIndices().back()->getNode()->getInst());
                        PrintVal(found->getBP()->getNode()->getVal());
                        PrintVal(found->getLoad());
                        PrintVal(found->getIndices().back()->getNode()->getInst());
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
                // this may be loading from a constant global structure
                // in that case we are interested in finding out which value we are pulling from the structure
                // this may or may not be possible, if the indices are or aren't statically determinable
                // ex: StencilChain/Naive(BB 170)
                if( con->getType()->isPointerTy() )
                {
                    bool canBeNull = false;
                    bool canBeFreed = false;
                    if( con->getPointerDereferenceableBytes(node->getInst()->getParent()->getParent()->getParent()->getDataLayout(), canBeNull, canBeFreed) < ALLOC_THRESHOLD )
                    {
                        // the pointer's allocation is not large enough, thus there is no collection that will represent it
                        // we still need this value in our expression, whatever it may be, so just make a constant symbol for it
                        auto newSymbol = make_shared<ConstantSymbol<int>>(0);
                        nodeToExpr[ opNode ] = newSymbol;
                        newSymbols.push_back(newSymbol); 
                    }
                }
            }
            else 
            {
                PrintVal(ld);
                for( const auto& coll : colls )
                {
                    PrintVal(coll->getBP()->getNode()->getVal());
                    for( const auto& idx : coll->getIndices() )
                    {
                        PrintVal(idx->getNode()->getInst());
                    }
                    if( coll->getLoad() )
                    {
                        PrintVal(coll->getLoad());
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
            if( const auto& nodeInst = dynamic_pointer_cast<Cyclebite::Graph::Inst>(opNode) )
            {
                vector<shared_ptr<Symbol>> args;
                for( const auto& binOp : bin->operands() )
                {
                    for( const auto& news : buildExpression(nodeInst, t, binOp, nodeToExpr, colls, vars) )
                    {
                        args.push_back( news );
                    }
                }
                vector<Cyclebite::Graph::Operation> binOps;
                binOps.push_back( Cyclebite::Graph::GetOp(bin->getOpcode()) );
                auto binExpr = make_shared<Expression>( t, args, binOps );
                nodeToExpr[ opNode ] = binExpr;
                newSymbols.push_back(binExpr);
            }
            else
            {
                throw CyclebiteException("Embedded binary instruction is not a Graph::Inst!");
            }
            // an in-task binary op should already have an expression in nodeToExpr, throw an error
            //PrintVal(bin);
            //PrintVal(opNode->getVal());
            //throw CyclebiteException("Cannot map this instruction to an expression!");
        }
        else if( const auto& phi = llvm::dyn_cast<llvm::PHINode>(op) )
        {
            // phis imply a loop-loop dependence or predication
            // if we have made it this far, this phi doesn't map to an RV.. 
            // but its operands might... multi-dimensional reductions store their result inside yet other phis...
            // in order to know that we need a predication expression, more than one of the incoming values must be live
            set<const llvm::Value*> liveIncomingValues;
            for( unsigned i = 0; i < phi->getNumIncomingValues(); i++ )
            {
                auto incomingBlock = phi->getIncomingBlock(i);
                if( Cyclebite::Graph::BBCBMap.contains(incomingBlock) )
                {
                    // it is live, put the value in the live set
                    liveIncomingValues.insert(phi->getIncomingValue(i));
                }
            }
            if( liveIncomingValues.size() > 1 )
            {
                /*auto predExpr = make_shared<PredicationExpression>(phi);
                nodeToExpr[ opNode ] = predExpr;
                newSymbols.push_back(predExpr);*/
                PrintVal(phi);
                throw CyclebiteException("Cannot yet build predication expressions for this task!");
            }
            else if( liveIncomingValues.empty() )
            {
                PrintVal(phi);
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
                binOps.push_back( Cyclebite::Graph::GetOp(bin->getOpcode()) );
                auto binExpr = make_shared<Expression>( t, args, binOps );
                nodeToExpr[ opNode ] = binExpr;
                newSymbols.push_back(binExpr);
            }
        }
        else if( const auto& cast = llvm::dyn_cast<llvm::CastInst>(op) )
        {
            if( const auto nodeInst = dynamic_pointer_cast<Cyclebite::Graph::Inst>( opNode ) )
            {
                vector<shared_ptr<Symbol>> args;
                for( const auto& castOp : cast->operands() )
                {
                    // call expression builder
                    for( const auto& news : buildExpression(nodeInst, t, castOp, nodeToExpr, colls, vars) )
                    {
                        args.push_back( news );
                    }
                }
                auto castExpr = make_shared<OperatorExpression>( t, Cyclebite::Graph::GetOp(cast->getOpcode()), args );
                nodeToExpr[ opNode ] = castExpr;
                newSymbols.push_back(castExpr);
            }
            else
            {
                throw CyclebiteException("Cast instruction is not a Graph::Inst!");
            }
        }
        else if( const auto& extr = llvm::dyn_cast<llvm::ExtractValueInst>(op) )
        {
            // this shims a vector to, say, a function argument that must be a scalar
            // thus we just skip this instruction and go to the source of its vector
            for( const auto& news : buildExpression(node, t, extr->getAggregateOperand(), nodeToExpr, colls, vars) )
            {
                nodeToExpr[ opNode ] = news;
                newSymbols.push_back(news);
            }
        }
    }
    else if( auto con = llvm::dyn_cast<llvm::Constant>(op) )
    {
        if( con->getType()->isIntegerTy() )
        {
            newSymbols.push_back(make_shared<ConstantSymbol<int64_t>>(*con->getUniqueInteger().getRawData()));
        }
        else if( con->getType()->isFloatTy() )
        {
            if( const auto& conF = llvm::dyn_cast<llvm::ConstantFP>(con) )
            {
                newSymbols.push_back(make_shared<ConstantSymbol<float>>( conF->getValueAPF().convertToFloat() ));
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
                newSymbols.push_back(make_shared<ConstantSymbol<double>>( conD->getValueAPF().convertToDouble() ));
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
            PrintVal(con);
            PrintVal(node->getInst());
            throw CyclebiteException("Cannot support undefined constants yet");
        }
        else if( const auto& convec = llvm::dyn_cast<llvm::ConstantVector>(op) )
        {
            for( unsigned i = 0; i < convec->getNumOperands(); i++ )
            {
                if( convec->getOperand(i)->getType()->isIntegerTy() )
                {
                    newSymbols.push_back(make_shared<ConstantSymbol<int64_t>>(*convec->getOperand(i)->getUniqueInteger().getRawData()));
                }
                else if( convec->getOperand(i)->getType()->isFloatTy() )
                {
                    if( const auto& conF = llvm::dyn_cast<llvm::ConstantFP>(convec->getOperand(i)) )
                    {
                        newSymbols.push_back(make_shared<ConstantSymbol<float>>( conF->getValueAPF().convertToFloat() ));
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
                        newSymbols.push_back(make_shared<ConstantSymbol<double>>( conD->getValueAPF().convertToDouble() ));
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
            PrintVal(op);
            PrintVal(node->getVal());
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
        PrintVal(op);
        PrintVal(node->getVal());
        throw CyclebiteException("Cannot recognize this operand type when building an expression!");
    }
    if( newSymbols.empty() )
    {
        PrintVal(op);
        throw CyclebiteException("Could not build a symbol for this llvm::value!");
    }
    return newSymbols;
}

const shared_ptr<Expression> Cyclebite::Grammar::constructExpression( const shared_ptr<Task>& t, 
                                                                      const vector<shared_ptr<Graph::Inst>>& insts, 
                                                                      const shared_ptr<ReductionVariable>& rv, 
                                                                      const set<shared_ptr<Collection>>& colls, 
                                                                      const set<shared_ptr<InductionVariable>>& vars )
{
    shared_ptr<Expression> expr;

    map<shared_ptr<Graph::DataValue>, shared_ptr<Symbol>> nodeToExpr;
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
            nodeToExpr[ Graph::DNIDMap.at(coll->getLoad()) ] = coll;
        }
        for( const auto& st : coll->getStores() )
        {
            nodeToExpr[ Graph::DNIDMap.at(st) ] = coll;
        }
    }
    // if there is a reduction variable, it's phi should be put into nodeToExpr
    // but not the rv's node - the node is a binary operator that carries out the reduction (it is accounted for in the reduction expression)
    if( rv )
    {
        for( const auto& addr : rv->getAddresses() )
        {
            nodeToExpr[ addr ] = rv;
        }
        //nodeToExpr[rv->getNode()] = rv;
    }
    // now we iterate (from start to finish) over the instructions in the expression, each time building a Symbol for each one, until all instructions in the expression have a symbol built for them
    vector<Cyclebite::Graph::Operation> ops;
    for( const auto& node : insts )
    {
        if( nodeToExpr.contains(node) )
        {
            // you have already been accounted for by a recursive symbol build (probably for an operatorExpression of some kind)
            // so we skip you entirely (because you are already embedded in someone else's expression)
            continue;
        }
        if( const auto& shuffle = llvm::dyn_cast<llvm::ShuffleVectorInst>(node->getInst()) )
        {
            // shuffles appear to be useful for concatenating elemnents of different vectors into the same vector
            // the only example so far that has used this is optimized (-O3) StencilChain/Naive, which uses an "identity shuffle" (example, StencilChain/Naive)
            // we don't support shufflevectors yet because they usually contain more than one operation at once, and may be transformed later
            // example: StencilChain/Naive/DFG_Kernel15.svg (shufflevector transforms i8 to i32, then i32 is converted to float before the MAC takes place)
            PrintVal(node->getInst());
            throw CyclebiteException("Cannot support shufflevector instructions yet!");
        }
        // this parameter maps to the place this instruction is stored
        // (see Cyclebite::Grammar::Expression::getStored() documentation for more on this)
        shared_ptr<Symbol> symbolOutput = nullptr;
        for( const auto& succ : node->getSuccessors() )
        {
            if( const auto& inst = dynamic_pointer_cast<Cyclebite::Graph::Inst>(succ->getSnk()) )
            {
                if( inst->getOp() == Cyclebite::Graph::Operation::store )
                {
                    if( !nodeToExpr.contains(inst) )
                    {
                        throw CyclebiteException("Could not map an expression's store to a Symbol!");
                    }
                    symbolOutput = nodeToExpr.at(inst);
                    break;
                }
            }
        }
        vector<shared_ptr<Symbol>> vec;
        if( node->isFunctionCall() || node->isCastOp() )
        {
            // build an operatorExpression for these
            vector<shared_ptr<Symbol>> args;
            if( node->isFunctionCall() )
            {
                for( const auto& argOp : llvm::cast<llvm::CallBase>(node->getInst())->args() )
                {
                    // call expression builder
                    for( const auto& news : buildExpression(node, t, argOp.get(), nodeToExpr, colls, vars) )
                    {
                        if( Graph::DNIDMap.contains(argOp.get()) )
                        {
                            nodeToExpr[ Graph::DNIDMap.at(argOp.get()) ] = news;
                        }
                        args.push_back( news );
                    }
                }
                const llvm::Function* f;
                if( llvm::cast<llvm::CallBase>(node->getInst())->getCalledFunction() == nullptr )
                {
                    // we have to find it in our dynamic information
                    for( const auto& succ : node->parent->getSuccessors() )
                    {
                        auto targetParent = static_pointer_cast<Cyclebite::Graph::ControlNode>(succ->getSnk());
                        for( const auto& bb : Cyclebite::Graph::BBCBMap )
                        {
                            if( bb.second == targetParent )
                            {
                                f = bb.first->getParent();
                                // confirm this function is not the same as the caller function
                                // this will break under recursion
                                if( f != node->getInst()->getParent()->getParent() )
                                {
                                    break;
                                }
                                else
                                {
                                    // wrong function, must have searched the wrong node successor
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
                    f = llvm::cast<llvm::CallBase>(node->getInst())->getCalledFunction();
                }
                expr = make_shared<FunctionExpression>(t, f, args, symbolOutput);
            }
            else
            {
                for( const auto& op : node->getInst()->operands() )
                {
                    // call expression builder
                    for( const auto& news : buildExpression(node, t, op, nodeToExpr, colls, vars) )
                    {
                        if( Graph::DNIDMap.contains(op) )
                        {
                            nodeToExpr[ Graph::DNIDMap.at(op) ] = news;
                        }
                        args.push_back( news );
                    }
                }
                expr = make_shared<OperatorExpression>(t, node->getOp(), args, symbolOutput);
            }
        }
        else
        {
            ops.push_back( Cyclebite::Graph::GetOp(node->getInst()->getOpcode()) );
            for( const auto& op : node->getInst()->operands() )
            {
                for( const auto& news : buildExpression(node, t, op, nodeToExpr, colls, vars) )
                {
                    if( Graph::DNIDMap.contains(op) )
                    {
                        nodeToExpr[ Graph::DNIDMap.at(op) ] = news;
                    }
                    vec.push_back(news);
                }
            }
            expr = make_shared<Expression>(t, vec, ops, symbolOutput);
        }
        nodeToExpr[node] = expr;
    }
    if( expr )
    {
        if( rv )
        {
            // wrap the constructed expr in a reduction
            vector<shared_ptr<Symbol>> redVec;
            vector<Cyclebite::Graph::Operation> redOp;
            redVec.push_back(expr);
            auto red = make_shared<Reduction>(t, rv, redVec, redOp, expr->getOutput());
            nodeToExpr[rv->getNode()] = red;
            expr = red;
        }
    }
    else
    {
        if( rv && insts.empty() )
        {
            vector<shared_ptr<Symbol>> vec;
            // we have a "counter" expression, where we just load something, add to it, then store it back
            // thus, there should be a constant in the add. Find it, put it into the vec list, and construct a reduction on it
            if( const auto& inst = dynamic_pointer_cast<Graph::Inst>(rv->getNode()) )
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
        if( !expr )
        {
            throw CyclebiteException("Could not generate an expression!");
        }
    }
    return expr;
}

shared_ptr<Expression> Cyclebite::Grammar::getExpression( const shared_ptr<Task>& t, 
                                                          const set<shared_ptr<Collection>>& colls, 
                                                          const set<shared_ptr<ReductionVariable>>& rvs, 
                                                          const set<shared_ptr<InductionVariable>>& vars )
{
    // the following DFG walk does both 1 and 2
    // 1. Group all function nodes together
    deque<shared_ptr<Cyclebite::Graph::Inst>> Q;
    set<shared_ptr<Cyclebite::Graph::DataValue>, Cyclebite::Graph::p_GNCompare> covered;
    set<shared_ptr<Cyclebite::Graph::Inst>, Cyclebite::Graph::p_GNCompare> functionGroup;
    for( const auto& c : t->getCycles() )
    {
        for( const auto& block : c->getBody() )
        {
            for( const auto& node : block->getInstructions() )
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
                                if( !Cyclebite::Graph::DNIDMap.contains(user) )
                                {
                                    continue;
                                }
                                else if( !t->find(Cyclebite::Graph::DNIDMap.at(user)) )
                                {
                                    continue;
                                }
                                // we expect eating stores to happen only after functional groups
                                if( const auto st = llvm::dyn_cast<llvm::StoreInst>(user) )
                                {
                                    covered.insert(Cyclebite::Graph::DNIDMap.at(st));
                                }
                                else if( const auto userInst = llvm::dyn_cast<llvm::Instruction>(user) )
                                {
                                    if( !covered.contains(Cyclebite::Graph::DNIDMap.at(userInst)) )
                                    {
                                        if( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(userInst))->isFunction() )
                                        {
                                            functionGroup.insert(static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(userInst)));
                                        }
                                        covered.insert(Cyclebite::Graph::DNIDMap.at(userInst));
                                        Q.push_back(static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(userInst)));
                                    }
                                }
                            }
                            for( const auto& use : Q.front()->getInst()->operands() )
                            {
                                if( Cyclebite::Graph::DNIDMap.find(use) == Cyclebite::Graph::DNIDMap.end() )
                                {
                                    continue;
                                }
                                else if( !t->find(Cyclebite::Graph::DNIDMap.at(use)) )
                                {
                                    continue;
                                }
                                // we expect feeding loads to happen only prior to functional groups
                                if( const auto ld = llvm::dyn_cast<llvm::LoadInst>(use) )
                                {
                                    covered.insert(Cyclebite::Graph::DNIDMap.at(ld));
                                }
                                else if( const auto useInst = llvm::dyn_cast<llvm::Instruction>(use) )
                                {
                                    if( !covered.contains(Cyclebite::Graph::DNIDMap.at(useInst)) )
                                    {
                                        if( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(useInst))->isFunction() )
                                        {
                                            functionGroup.insert(static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(useInst)));
                                        }
                                        covered.insert(Cyclebite::Graph::DNIDMap.at(useInst));
                                        Q.push_back(static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(useInst)));
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
    if( functionGroup.empty() )
    {
        throw CyclebiteException("Function group is empty!");
    }
    // 2. order the instructions from first (the instruction that doesn't have any function group inputs) to last (the instruction that feeds the store)
    // 2a. find the first instruction
    shared_ptr<Cyclebite::Graph::Inst> first = nullptr;
    // look for an instruction that has only inputs from the memory group
    // if there isn't an instruction with only memory inputs (like a reduction expression), we pick the one with the most memory inputs
    map<shared_ptr<Graph::Inst>, uint32_t> memoryInputs;
    for( const auto& inst : functionGroup )
    {
        bool allMemory = true;
        for( const auto& pred : inst->getPredecessors() )
        {
            if( const auto& predInst = dynamic_pointer_cast<Graph::Inst>(pred->getSrc()) )
            {
                if( !predInst->isMemory() )
                {
                    allMemory = false;
                }
                else
                {
                    memoryInputs[inst]++;
                }
            }
        }
        if( allMemory )
        {
            first = inst;
        }
    }
    if( !first )
    {
        // pick the inst with the most memory inputs
        // if there is a tie, we throw exception
        map<uint32_t, set<shared_ptr<Graph::Inst>>> sortedWinners;
        for( const auto& inst : memoryInputs )
        {
            sortedWinners[inst.second].insert(inst.first);
        }
        if( sortedWinners.begin()->second.size() > 1 )
        {
            throw CyclebiteException("Could not find first instruction in the instruction group!");
        }
        else
        {
            first = *sortedWinners.begin()->second.begin();
        }
    }

    // 2b. order the function group starting from the first instruction
    vector<shared_ptr<Cyclebite::Graph::Inst>> order;
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
                    if( functionGroup.find(Cyclebite::Graph::DNIDMap.at(opInst)) != functionGroup.end() )
                    {
                        // this instruction comes before the current, thus two things need to happen
                        // 1. it needs to be pushed before the current instruction in the "order" list
                        // 2. its operands need to be investigated before anything else
                        auto pos = std::find(order.begin(), order.end(), Cyclebite::Graph::DNIDMap.at(instQ.front()));
                        if( pos == order.end() )
                        {
                            throw CyclebiteException("Cannot resolve where to insert function inst operand in the ordered list!");
                        }
                        order.insert(pos, static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(opInst)));
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
                    if( functionGroup.find(Cyclebite::Graph::DNIDMap.at(userInst)) != functionGroup.end() )
                    {
                        order.push_back(static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(userInst)));
                        instQ.push_back(userInst);
                        instCovered.insert(userInst);
                    }
                }
            }
        }
        instQ.pop_front();
    }
    return constructExpression(t, order, *rvs.begin(), colls, vars);
}