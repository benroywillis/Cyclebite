#include "Expression.h"
#include "Reduction.h"
#include "BasePointer.h"
#include "ConstantSymbol.h"
#include "ConstantFunction.h"
#include "Task.h"
#include "TaskParameter.h"
#include "OperatorExpression.h"
#include "Graph/inc/IO.h"
#include "Util/Exceptions.h"
#include "Util/Print.h"
#include <iostream>

using namespace std;
using namespace Cyclebite::Grammar;

bool Expression::printedName = false;

Expression::Expression( const vector<shared_ptr<Symbol>>& s, const vector<Cyclebite::Graph::Operation>& o ) : Symbol("expr"), ops(o), symbols(s) 
{
    if( s.empty() )
    {
        throw AtlasException("Expression cannot be empty!");
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
        throw AtlasException("There should be "+to_string(symbols.size()-1)+" operations for an expression with "+to_string(symbols.size())+" symbols! Operation count: "+to_string(o.size()));
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
                                            map<shared_ptr<Cyclebite::Graph::DataValue>, shared_ptr<Expression>>& nodeToExpr, 
                                            const set<shared_ptr<Collection>>& colls )
{
    vector<shared_ptr<Symbol>> newSymbols;
    if( const auto inst = llvm::dyn_cast<llvm::Instruction>(op) )
    {
        auto opNode = Cyclebite::Graph::DNIDMap.at(inst);
        if( nodeToExpr.find(Cyclebite::Graph::DNIDMap.at(inst)) != nodeToExpr.end() )
        {
            // this value comes from a previous function group operator, thus it should be a symbol in the expression
            newSymbols.push_back(nodeToExpr.at(Cyclebite::Graph::DNIDMap.at(inst)));
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
                if( coll->getElementPointer() == ld )
                {
#ifdef DEBUG
                    if( found )
                    {
                        PrintVal(ld);
                        PrintVal(coll->getBP()->getNode()->getVal());
                        PrintVal(coll->getElementPointer());
                        PrintVal(coll->getIndices().back()->getNode()->getInst());
                        PrintVal(found->getBP()->getNode()->getVal());
                        PrintVal(found->getElementPointer());
                        PrintVal(found->getIndices().back()->getNode()->getInst());
                        throw AtlasException("Mapped more than one collection to a load value!");
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
                        newSymbols.push_back(make_shared<ConstantSymbol<int>>(0)); 
                    }
                }
            }
            else 
            {
                PrintVal(ld);
                for( const auto& coll : colls )
                {
                    PrintVal(coll->getBP()->getNode()->getVal());
                }
                throw AtlasException("Could not find a collection to describe this load!");
            }
        }
        else if( const auto st = llvm::dyn_cast<llvm::StoreInst>(inst) )
        {
            // this instruction stores the function group result, do nothing for now
        }
        else if( const auto bin = llvm::dyn_cast<llvm::BinaryOperator>(inst) )
        {
            // a binary op in a series of ops
            // there should be an expression for the input(s) of this op
            if( nodeToExpr.find(opNode) != nodeToExpr.end() )
            {
                newSymbols.push_back(nodeToExpr.at(opNode));
            }
            else if( !t->find(opNode) )
            {
                // this is an out-of-task expression
                // make a placeholder and deal with it later
                ( make_shared<TaskParameter>(opNode, t) );
            }
            else
            {
                PrintVal(bin);
                PrintVal(opNode->getVal());
                throw AtlasException("Cannot map this instruction to an expression!");
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
                throw AtlasException("Could not extract float from constant float!");
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
                throw AtlasException("Could not extract double from constant double!");
            }
        }
        else if( const auto& undef = llvm::dyn_cast<llvm::UndefValue>(op) )
        {
            // the value is a constant that is not known till runtime
            // llvm has undefined values as a result of the optimizer to allow for constant-qualified values that aren't known till runtime
            // we don't support this yet
            PrintVal(con);
            PrintVal(node->getInst());
            throw AtlasException("Cannot support undefined constants yet");
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
                        throw AtlasException("Could not extract float from constant float!");
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
                        throw AtlasException("Could not extract double from constant double!");
                    }
                }
            }
        }
        else if( const auto& func = llvm::dyn_cast<llvm::Function>(con) )
        {
            // we have found a function call that returns a value, more generall a "global object" in the llvm api
            // here we implement a whitelist that allows certain functions that we know we can handle
            // for example; rand(), sort(), qsort() and other libc function calls are on the whitelist
            // for now we just insert the function name and hope for the best
            newSymbols.push_back(make_shared<ConstantFunction>(func));
        }
        else
        {
            PrintVal(op);
            PrintVal(node->getVal());
            throw AtlasException("Constant used in an expression is not an integer!");
        }
    }
    else if( const auto& arg = llvm::dyn_cast<llvm::Argument>(op) )
    {
        // this value comes from somewhere else
        // assign a placeholder for it
        newSymbols.push_back( make_shared<TaskParameter>(Cyclebite::Graph::DNIDMap.at(arg), t) );
    }
    else
    {
        PrintVal(op);
        PrintVal(node->getVal());
        throw AtlasException("Cannot recognize this operand type when building an expression!");
    }
    if( newSymbols.empty() )
    {
        PrintVal(op);
        throw AtlasException("Could not build a symbol for this llvm::value!");
    }
    return newSymbols;
}

const shared_ptr<Expression> Cyclebite::Grammar::constructExpression( const shared_ptr<Task>& t, const vector<shared_ptr<Graph::Inst>>& insts, const shared_ptr<ReductionVariable>& rv, const set<shared_ptr<Collection>>& colls )
{
    shared_ptr<Expression> expr;

    vector<Cyclebite::Graph::Operation> ops;
    map<shared_ptr<Graph::DataValue>, shared_ptr<Expression>> nodeToExpr;
    for( const auto& node : insts )
    {
        if( const auto& shuffle = llvm::dyn_cast<llvm::ShuffleVectorInst>(node->getInst()) )
        {
            // shuffles appear to be useful for concatenating elemnents of different vectors into the same vector
            // the only example so far that has used this is optimized (-O3) StencilChain/Naive, which uses an "identity shuffle" (example, StencilChain/Naive)
            // we don't support shufflevectors yet because they usually contain more than one operation at once, and may be transformed later
            // example: StencilChain/Naive/DFG_Kernel15.svg (shufflevector transforms i8 to i32, then i32 is converted to float before the MAC takes place)
            PrintVal(node->getInst());
            throw AtlasException("Cannot support shufflevector instructions yet!");
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
            }
            expr = make_shared<OperatorExpression>(node->getOp(), args);
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
                                throw AtlasException("Could not extract float from constant float!");
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
                                throw AtlasException("Could not extract double from constant double!");
                            }
                        }
                    }
                }
            }
            expr = make_shared<Reduction>(rv, vec, ops);
        }
        if( !expr )
        {
            throw AtlasException("Could not generate an expression!");
        }
    }
    return expr;
}