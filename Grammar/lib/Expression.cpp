// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "Expression.h"
#include "Reduction.h"
#include "BasePointer.h"
#include "ConstantSymbol.h"
#include "FunctionExpression.h"
#include "Task.h"
#include "TaskParameter.h"
#include "OperatorExpression.h"
#include "Graph/inc/IO.h"
#include "Util/Exceptions.h"
#include "Util/Print.h"
#include "llvm/IR/Constants.h"
#include <iostream>

using namespace std;
using namespace Cyclebite::Grammar;

bool Expression::printedName = false;

Expression::Expression( const vector<shared_ptr<Symbol>>& s, const vector<Cyclebite::Graph::Operation>& o ) : Symbol("expr"), ops(o), symbols(s) 
{
    if( s.empty() )
    {
        throw CyclebiteException("Expression cannot be empty!");
    }
    printedName = false;
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

Expression::Expression( const vector<shared_ptr<Symbol>> s, const vector<Cyclebite::Graph::Operation> o, const string name ) : Symbol(name), ops(o), symbols(s) {} 

string Expression::dump() const
{
    string expr;
    if( !printedName )
    {
        expr = name + " = ";
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
    return expr;
}

const vector<shared_ptr<Symbol>>& Expression::getSymbols() const
{
    return symbols;
}

const set<shared_ptr<InductionVariable>>& Expression::getVars() const
{
    return vars;
}

const set<shared_ptr<Collection>> Expression::getCollections() const
{
    set<shared_ptr<Collection>> collections;
    collections.insert(inputs.begin(), inputs.end());
    collections.insert(outputs.begin(), outputs.end());
    return collections;
}

const set<shared_ptr<Collection>>& Expression::getInputs() const
{
    return inputs;
}

const set<shared_ptr<Collection>>& Expression::getOutputs() const
{
    return outputs;
}

vector<shared_ptr<Symbol>> buildExpression( const shared_ptr<Cyclebite::Graph::Inst>& node,
                                            const shared_ptr<Task>& t,  
                                            const llvm::Value* op, 
                                            map<shared_ptr<Cyclebite::Graph::DataValue>, shared_ptr<Symbol>>& nodeToExpr, 
                                            const set<shared_ptr<Collection>>& colls )
{
    vector<shared_ptr<Symbol>> newSymbols;
    if( const auto inst = llvm::dyn_cast<llvm::Instruction>(op) )
    {
        const auto opNode = Cyclebite::Graph::DNIDMap.at(op);
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
            // an in-task binary op should already have an expression in nodeToExpr, throw an error
            PrintVal(bin);
            PrintVal(opNode->getVal());
            throw CyclebiteException("Cannot map this instruction to an expression!");
        }
        else if( const auto& phi = llvm::dyn_cast<llvm::PHINode>(op) )
        {
            // phis imply a loop-loop dependence or predication
            // if twe have made it this far, this phi doesn't map to an RV.. so at this point it is an error
            PrintVal(phi);
            throw CyclebiteException("phi node use implies predication or loop-loop dependence!");
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
                for( const auto& news : buildExpression(node, t, argOp.get(), nodeToExpr, colls) )
                {
                    args.push_back( news );
                }
            }
            auto newSymbol = make_shared<FunctionExpression>(func, args);
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

const shared_ptr<Expression> Cyclebite::Grammar::constructExpression( const shared_ptr<Task>& t, const vector<shared_ptr<Graph::Inst>>& insts, const shared_ptr<ReductionVariable>& rv, const set<shared_ptr<Collection>>& colls )
{
    shared_ptr<Expression> expr;

    vector<Cyclebite::Graph::Operation> ops;
    map<shared_ptr<Graph::DataValue>, shared_ptr<Symbol>> nodeToExpr;
    if( rv )
    {
        // the reduction variable's phi should be put into nodeToExpr
        // but not its node - the node is the literal reduction and this is implicit in the reduction expression
        if( rv->getPhi() )
        {
            nodeToExpr[Graph::DNIDMap.at(rv->getPhi())] = rv;
        }
        nodeToExpr[rv->getNode()] = rv;
    }
    for( const auto& node : insts )
    {
        if( const auto& shuffle = llvm::dyn_cast<llvm::ShuffleVectorInst>(node->getInst()) )
        {
            // shuffles appear to be useful for concatenating elemnents of different vectors into the same vector
            // the only example so far that has used this is optimized (-O3) StencilChain/Naive, which uses an "identity shuffle" (example, StencilChain/Naive)
            // we don't support shufflevectors yet because they usually contain more than one operation at once, and may be transformed later
            // example: StencilChain/Naive/DFG_Kernel15.svg (shufflevector transforms i8 to i32, then i32 is converted to float before the MAC takes place)
            PrintVal(node->getInst());
            throw CyclebiteException("Cannot support shufflevector instructions yet!");
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
                    for( const auto& news : buildExpression(node, t, argOp.get(), nodeToExpr, colls) )
                    {
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
                expr = make_shared<FunctionExpression>(f, args);
            }
            else
            {
                for( const auto& op : node->getInst()->operands() )
                {
                    // call expression builder
                    for( const auto& news : buildExpression(node, t, op, nodeToExpr, colls) )
                    {
                        args.push_back( news );
                    }
                }
                expr = make_shared<OperatorExpression>(node->getOp(), args);
            }
        }
        else
        {
            ops.push_back( Cyclebite::Graph::GetOp(node->getInst()->getOpcode()) );
            for( const auto& op : node->getInst()->operands() )
            {
                for( const auto& news : buildExpression(node, t, op, nodeToExpr, colls) )
                {
                    vec.push_back(news);
                }
            }
            if( rv )
            {
                expr = make_shared<Reduction>(rv, vec, ops);
                nodeToExpr[rv->getNode()] = expr;
            }
            else
            {
                expr = make_shared<Expression>(vec, ops);
            }
        }
        nodeToExpr[node] = expr;
    }
    if( !expr )
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
            expr = make_shared<Reduction>(rv, vec, ops);
        }
        if( !expr )
        {
            throw CyclebiteException("Could not generate an expression!");
        }
    }
    return expr;
}