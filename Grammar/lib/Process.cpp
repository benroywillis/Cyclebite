//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Process.h"
#include "Graph/inc/DataGraph.h"
#include "Graph/inc/ControlBlock.h"
#include "ConstantSymbol.h"
#include "Graph/inc/IO.h"
#include "Expression.h"
#include "IO.h"
#include "Reduction.h"
#include "IndexVariable.h"
#include "ConstantArray.h"
#include "Util/Annotate.h"
#include "Util/Print.h"
#include "Task.h"
#include "Graph/inc/Dijkstra.h"
#include "BasePointer.h"
#include "Export.h"
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <deque>

using namespace Cyclebite::Grammar;
using namespace Cyclebite::Graph;
using namespace std;

void DisectConstant( vector<shared_ptr<Symbol>>& vec, const llvm::Constant* con)
{
    if( con->getType()->isIntegerTy() )
    {
        vec.push_back(make_shared<ConstantSymbol>(con, con->getUniqueInteger().getRawData(), ConstantType::INT64));
    }
    else if( con->getType()->isFloatTy() )
    {
        if( const auto& conF = llvm::dyn_cast<llvm::ConstantFP>(con) )
        {
            float val = conF->getValueAPF().convertToFloat();
            vec.push_back(make_shared<ConstantSymbol>( con, &val, ConstantType::FLOAT ));
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
            vec.push_back(make_shared<ConstantSymbol>( con, &val, ConstantType::DOUBLE ));
        }
        else
        {
            throw CyclebiteException("Could not extract double from constant double!");
        }
    }
    else
    {
        Cyclebite::Util::PrintVal(con);
        throw CyclebiteException("Cannot recognize this constant type!");
    }
}

bool hasConstantOffset(const llvm::GetElementPtrInst* gep )
{
    bool allConstant = true;
    for( const auto& idx : gep->indices() )
    {
        if( !llvm::isa<llvm::Constant>(idx) )
        {
            allConstant = false;
            break;
        }
    }
    return allConstant;
}

vector<shared_ptr<InductionVariable>> getOrdering( const llvm::GetElementPtrInst* gep, const set<shared_ptr<InductionVariable>>& IVs )
{
    vector<shared_ptr<InductionVariable>> order;
    for( auto idx = gep->idx_begin(); idx != gep->idx_end(); idx++ )
    {
        bool found = false;
        for( const auto& iv : IVs )
        {
            if( (iv->getNode()->getVal() == *idx) || (iv->isOffset(*idx)) )
            {
                found = true;
                order.push_back(iv);
                break;
            }
            else if( const auto& con = llvm::dyn_cast<llvm::Constant>(*idx) )
            {
                // skip
                found = true;
                break;
            }
            else if( const auto& glob = llvm::dyn_cast<llvm::GlobalValue>(*idx) )
            {
                // not sure what to do here
                throw CyclebiteException("Found a global in a gep!");
            }
        }
        if( !found )
        {
            Cyclebite::Util::PrintVal(gep);
            throw CyclebiteException("Cannot map a gep index to an induction variable!");
        }
    }
    return order;
}

void Cyclebite::Grammar::Process(const set<shared_ptr<Task>, TaskIDCompare>& tasks)
{
    // each expression maps 1:1 with tasks from the cartographer
    map<shared_ptr<Task>, vector<shared_ptr<Expression>>> taskToExpr;
    for( const auto& t : tasks )
    {
        vector<shared_ptr<Expression>> exprs;
        try
        {
#ifdef DEBUG
            cout << endl;
            spdlog::info("Task "+to_string(t->getID()));
#endif
            // get all induction variables
            auto vars = getInductionVariables(t);
#ifdef DEBUG
            spdlog::info("Induction Variables:");
            for( const auto& var : vars )
            {
                spdlog::info(var->dump()+" -> "+Cyclebite::Util::PrintVal(var->getNode()->getVal(), false));
            }            
#endif
            // get all reduction variables
            auto rvs = getReductionVariables(t, vars);
#ifdef DEBUG
            spdlog::info("Reductions");
            for( const auto& rv : rvs )
            {
                spdlog::info(rv->dump()+" -> "+Cyclebite::Util::PrintVal(rv->getNode()->getVal(), false));
            }
#endif
            // get all base pointers
            auto bps  = getBasePointers(t);
#ifdef DEBUG
            spdlog::info("Base Pointers");
            for( const auto& bp : bps )
            {
                spdlog::info(bp->dump()+" -> "+Cyclebite::Util::PrintVal(bp->getNode()->getVal(), false));
            }
#endif
            // get index variables
            auto idxVars = getIndexVariables(t, vars);
#ifdef DEBUG
            spdlog::info("Index Variables:");
            for( const auto& idx : idxVars )
            {
                string dimension = "";
                dimension = "(dimension "+to_string(idx->getDimensionIndex())+") ";
                spdlog::info(dimension+idx->dump()+" -> "+Cyclebite::Util::PrintVal(idx->getNode()->getVal(), false)+" ("+Cyclebite::Util::PrintVal(idx->getInst()->getInst(), false)+")");
            }
#endif
            // gather constants that are important to the expression of our task
            auto cons   = getConstants(t, idxVars);
#ifdef DEBUG
            spdlog::info("Constant expressions:");
            for( const auto& c : cons )
            {
                spdlog::info(c->dump()+" -> "+Cyclebite::Util::PrintVal(c->getConstant(), false));
            }
#endif
            // construct collections
            auto cs   = getCollections(t, bps, idxVars);
#ifdef DEBUG
            spdlog::info("Collections:");
            for( const auto& c : cs )
            {
                spdlog::info(c->dump());
            }
#endif
            // each task should have exactly one expression
            exprs = getExpressions(t, cs, rvs, cons, vars);
#ifdef DEBUG
            spdlog::info("Expressions:");
            for( const auto& expr : exprs )
            {
                spdlog::info("\t"+expr->dump());
            }
            spdlog::info("Grammar Success");
#endif
        }
        catch(CyclebiteException& e)
        {
            spdlog::critical(e.what());
#ifdef DEBUG
            cout << endl;
#endif
        }
        taskToExpr[t] = exprs;
    }
    Export(taskToExpr);
}