//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "IO.h"
#include "IndexVariable.h"
#include "InductionVariable.h"
#include "BasePointer.h"
#include "Collection.h"
#include "Graph/inc/IO.h"
#include "Util/Exceptions.h"
#include "Util/Print.h"
#include <spdlog/spdlog.h>

using namespace std;
using namespace Cyclebite::Grammar;

set<shared_ptr<Cyclebite::Graph::Inst>, Cyclebite::Graph::p_GNCompare> Cyclebite::Grammar::SignificantMemInst;

void Cyclebite::Grammar::InjectSignificantMemoryInstructions(const nlohmann::json& instanceJson, const map<int64_t, llvm::Value*>& IDToValue)
{
    if( instanceJson.find("Instruction Tuples") == instanceJson.end() )
    {
        spdlog::critical("Could not find 'Instruction Tuples' category in input instance file!");
        throw CyclebiteException("Could not find 'Instruction Tuples' category in input instance file!");
    }
    //for( const auto& value : instanceJson["Instruction Tuples"].items() )
    for( const auto& value : instanceJson["Instruction Tuples"] )
    {
        //auto val = IDToValue.at(stol(value.key()));
        auto val = IDToValue.at(value);
        if( val )
        {
            if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(val) )
            {
                if( Cyclebite::Graph::DNIDMap.find(inst) == Cyclebite::Graph::DNIDMap.end() )
                {
                    throw CyclebiteException("Found a significant memory op that's not live!");
                }
                // mark as significant
                SignificantMemInst.insert( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(inst)) );
            }
            else
            {
                PrintVal(val);
                throw CyclebiteException("Significant memory op is not an instruction!");
            }
        }
        else
        {
            throw CyclebiteException("Cannot find significant memory op ID in the value ID map!");
        }
    }
}

inline string getInstName( uint64_t NID, const llvm::Value* v )
{
    string name = "";
    string instString = PrintVal(v, false);
    if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(v) )
    {
        auto start = instString.find("%");
        auto end = instString.find("%", start+1) - start;
        name = instString.substr(start, end);
    }
    else if( const auto& arg = llvm::dyn_cast<llvm::Argument>(v) )
    {
        if( arg->getParent() )
        {
            name += string(arg->getParent()->getName()) + " <- ";
        }
        name += instString;
    }
    else
    {
        name = instString;
    }
    return "\t"+to_string(NID)+" [label=\""+name+"\"];\n";
}

string Cyclebite::Grammar::PrintIdxVarTree( const set<shared_ptr<IndexVariable>>& idxVars )
{
    string dotString = "digraph{\n\trankdir=\"BT\";\n";
    for( const auto& idx : idxVars )
    {
        dotString += getInstName(idx->getNode()->NID, idx->getNode()->getInst());
        if( idx->getIV() )
        {
            dotString += getInstName(idx->getIV()->getNode()->NID, idx->getIV()->getNode()->getVal());
        }
    }
    for( const auto& idx : idxVars )
    {
        for( const auto& p : idx->getParents() )
        {
            dotString += "\t"+to_string(idx->getNode()->NID)+" -> "+to_string(p->getNode()->NID)+";\n";
        }
        if( idx->getIV() )
        {
            dotString += "\t"+to_string(idx->getIV()->getNode()->NID)+" -> "+to_string(idx->getNode()->NID)+" [style=dotted];\n";
        }
    }
    dotString += "}";
    return dotString;
}

string Cyclebite::Grammar::VisualizeCollection( const shared_ptr<Collection>& coll )
{
    string dotString = "digraph{\n\trankdir=\"BT\";\n";
    for( const auto& bp : coll->getBPs() )
    {
        dotString += getInstName(bp->getNode()->NID, bp->getNode()->getVal());
    }
    for( const auto& idx : coll->getIndices() )
    {
        dotString += getInstName(idx->getNode()->NID, idx->getNode()->getInst());
        if( idx->getIV() )
        {
            dotString += getInstName(idx->getIV()->getNode()->NID, idx->getIV()->getNode()->getVal());
        }
    }
    // print the base pointer edges
    // they should point to their parent-most idxVar
    for( const auto& bp : coll->getBPs() )
    {
        for( const auto& idx : coll->getIndices() )
        {
            if( idx->getBPs().contains(bp) )
            {
                dotString += "\t"+to_string(idx->getNode()->NID)+" -> "+to_string(bp->getNode()->NID)+" [style=dashed];\n";
            }
        }
    }
    for( const auto& idx : coll->getIndices() )
    {
        for( const auto& p : idx->getParents() )
        {
            if( std::find(coll->getIndices().begin(), coll->getIndices().end(), p) != coll->getIndices().end() )
            {
                dotString += "\t"+to_string(idx->getNode()->NID)+" -> "+to_string(p->getNode()->NID)+";\n";
            }
        }
        if( idx->getIV() )
        {
            dotString += "\t"+to_string(idx->getIV()->getNode()->NID)+" -> "+to_string(idx->getNode()->NID)+" [style=dotted];\n";
        }
    }
    dotString += "}";
    return dotString;
}