//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "IO.h"
#include "IndexVariable.h"
#include "InductionVariable.h"
#include "BasePointer.h"
#include "Collection.h"
#include "Task.h"
#include "Graph/inc/IO.h"
#include "Util/Exceptions.h"
#include "Util/Print.h"
#include "Util/Annotate.h"
#include <llvm/IR/Module.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <spdlog/spdlog.h>

using namespace std;
using namespace Cyclebite::Grammar;

map<string, vector<string>> Cyclebite::Grammar::fileLines;
map<uint32_t, pair<string,uint32_t>> Cyclebite::Grammar::blockToSource;
set<shared_ptr<Cyclebite::Graph::Inst>, Cyclebite::Graph::p_GNCompare> Cyclebite::Grammar::SignificantMemInst;

void Cyclebite::Grammar::InitSourceMaps(const std::unique_ptr<llvm::Module>& SourceBitcode)
{
    set<string> foundSources;
    // first, map basic block IDs to their source code lines
    for (auto f = SourceBitcode->begin(); f != SourceBitcode->end(); f++)
    {
        for (auto b = f->begin(); b != f->end(); b++)
        {
            for( auto ii = b->begin(); ii != b->end(); ii++ )
            {
                if( !llvm::isa<llvm::DbgInfoIntrinsic>(ii) )
                {
                    const auto& LOC = ii->getDebugLoc();
                    if( LOC.getAsMDNode() != nullptr )
                    {
                        if( auto scope = llvm::dyn_cast<llvm::DIScope>(LOC.getScope()) )
                        {
                            string dir = string(scope->getFile()->getDirectory());
                            dir.append("/");
                            dir.append(scope->getFile()->getFilename());
                            foundSources.insert(dir);
                            blockToSource[(uint32_t)Cyclebite::Util::GetBlockID(cast<llvm::BasicBlock>(b))] = pair(dir, LOC.getLine()-1); // we subtract one from the line bc file lines start at 1
                            break;
                        }
                    }
                }
            }
        }
    }
    // second, map source files to their lines in a way that makes line injection convenient
    for( const auto& file : foundSources )
    {
        ifstream fin(file);
        string line;
        while( getline(fin, line) )
        {
            fileLines[file].push_back(line);
        }
    }
}

void Cyclebite::Grammar::InjectSignificantMemoryInstructions(const nlohmann::json& instanceJson, const map<int64_t, const llvm::Value*>& IDToValue)
{
    if( instanceJson.find("Instruction Tuples") == instanceJson.end() )
    {
        spdlog::critical("Could not find 'Instruction Tuples' category in input instance file!");
        throw CyclebiteException("Could not find 'Instruction Tuples' category in input instance file!");
    }
    //for( const auto& value : instanceJson["Instruction Tuples"].items() )
    for( const auto& value : instanceJson["Instruction Tuples"] )
    {
        if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(IDToValue.at(value)) )
        {
            if( Cyclebite::Graph::DNIDMap.find(inst) == Cyclebite::Graph::DNIDMap.end() )
            {
                PrintVal(inst);
                throw CyclebiteException("Found a significant memory op that's not live!");
            }
            // mark as significant
            SignificantMemInst.insert( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(inst)) );
        }
        else
        {
            PrintVal(IDToValue.at(value));
            throw CyclebiteException("Significant memory op is not an instruction!");
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
        dotString += getInstName(idx->getNode()->ID(), idx->getNode()->getInst());
        for( const auto& iv : idx->getDimensions() )
        {
            dotString += getInstName(iv->getNode()->ID(), iv->getNode()->getVal());
        }
    }
    for( const auto& idx : idxVars )
    {
        for( const auto& p : idx->getParents() )
        {
            dotString += "\t"+to_string(idx->getNode()->ID())+" -> "+to_string(p->getNode()->ID())+";\n";
        }
        for( const auto& iv : idx->getDimensions() )
        {
            dotString += "\t"+to_string(iv->getNode()->ID())+" -> "+to_string(idx->getNode()->ID())+" [style=dotted];\n";
        }
    }
    dotString += "}";
    return dotString;
}

string Cyclebite::Grammar::VisualizeCollection( const shared_ptr<Collection>& coll )
{
    string dotString = "digraph{\n\trankdir=\"BT\";\n";
    dotString += getInstName(coll->getBP()->getNode()->ID(), coll->getBP()->getNode()->getVal());
    for( const auto& idx : coll->getIndices() )
    {
        dotString += getInstName(idx->getNode()->ID(), idx->getNode()->getInst());
        for( const auto& iv : idx->getDimensions() )
        {
            dotString += getInstName(iv->getNode()->ID(), iv->getNode()->getVal());
        }
    }
    // print the base pointer edges
    // they should point to their parent-most idxVar
    for( const auto& idx : coll->getIndices() )
    {
        dotString += "\t"+to_string(idx->getNode()->ID())+" -> "+to_string(coll->getBP()->getNode()->ID())+" [style=dashed];\n";
    }
    for( const auto& idx : coll->getIndices() )
    {
        for( const auto& p : idx->getParents() )
        {
            if( std::find(coll->getIndices().begin(), coll->getIndices().end(), p) != coll->getIndices().end() )
            {
                dotString += "\t"+to_string(idx->getNode()->ID())+" -> "+to_string(p->getNode()->ID())+";\n";
            }
        }
        for( const auto& iv : idx->getDimensions() )
        {
            dotString += "\t"+to_string(iv->getNode()->ID())+" -> "+to_string(idx->getNode()->ID())+" [style=dotted];\n";
        }
    }
    dotString += "}";
    return dotString;
}

// this set keeps track of the lines we have already injected
// when inlined functions contain parallelizable loops, we will get multiple requests to parallelize that loop
// but we only want to do it once, so this set ensures that
set<uint32_t> linesInjected;
void updateSourceLines(uint32_t lineNo)
{
    // now update the blockToSource map to move everyone that sits "below" this injected line up one
    for( auto& block : blockToSource )
    {
        if( block.second.second >= lineNo )
        {
            block.second.second++;
        }
    }
    set<uint32_t> toInsert;
    for( auto entry : linesInjected )
    {
        if( entry >= lineNo )
        {
            toInsert.insert(entry+1);
        }
    }
    linesInjected.insert(toInsert.begin(), toInsert.end());
}

void InjectParallelFor( const shared_ptr<Cycle>& spot, uint32_t lineNo )
{
    auto injectString = "#pragma omp parallel for";
    if( spot->getTask()->getSourceFiles().size() != 1 )
    {
        throw CyclebiteException("Cannot yet support exporting OMP pragmas to tasks that map to more than one source file!");
    }
    auto injectSource = *(spot->getTask()->getSourceFiles().begin());
    fileLines[injectSource].insert(next( fileLines.at(injectSource).begin()+lineNo), injectString);

    updateSourceLines(lineNo);
}

void InjectSIMD( const shared_ptr<Cycle>& spot, uint32_t lineNo )
{
    auto injectString = "#pragma omp simd";
    if( spot->getTask()->getSourceFiles().size() != 1 )
    {
        throw CyclebiteException("Cannot yet support exporting OMP pragmas to tasks that map to more than one source file!");
    }
    auto injectSource = *(spot->getTask()->getSourceFiles().begin());
    fileLines[injectSource].insert(next( fileLines.at(injectSource).begin()+lineNo), injectString);

    updateSourceLines(lineNo);
}

uint32_t getLineNo(const shared_ptr<Cycle>& spot)
{
    uint32_t lineNo = UINT32_MAX;
    for( const auto& b : spot->getBody() )
    {
        // the line we want to inject before is the lowest number
        for( const auto id : b->originalBlocks )
        {
            // sometimes blocks will contain no debug info, which means we can't find a location to inject anything at the beginning of this cycle
            if( blockToSource.contains(id) )
            {
                if( (blockToSource.at(id).second) && (blockToSource.at(id).second < lineNo) )
                {
                    lineNo = blockToSource.at(id).second;
                    // this is a check to see if we have already injected at this spot
                    // when functions are inlined, this can result in many "spots" pointing to the same loop inside an inlined function
                    // thus, we check for redundancy here
                    if( linesInjected.contains(lineNo) )
                    {
                        spdlog::warn("Already injected at line "+to_string(lineNo));
                        return 0;
                    }
                    linesInjected.insert(lineNo);
                }
            }
            else
            {
                spdlog::warn("Basic block "+to_string(id)+" did not contain any location info. OMP pragma injection at this parallel cycle is impossible.");
            }
        }
    }
    // here we confirm that the line we found was right before a for loop
    if( spot->getTask()->getSourceFiles().size() != 1 )
    {
        throw CyclebiteException("Cannot yet support exporting OMP pragmas to tasks that map to more than one source file!");
    }
    auto injectSource = *(spot->getTask()->getSourceFiles().begin());
    auto original = lineNo;
    while( fileLines[injectSource][lineNo+1].find("for") == std::string::npos )
    {
        lineNo--;
        // we shouldn't deviate more than 5 lines or so
        if( lineNo < (original-5) )
        {
            spdlog::warn("Could not find a valid line number for parallel cycle "+to_string(spot->getID())+" in Task"+to_string(spot->getTask()->getID()));
            return 0;
        }
    }
    return lineNo;
}

void Cyclebite::Grammar::OMPAnnotateSource( const set<shared_ptr<Cycle>>& parallelSpots, const set<shared_ptr<Cycle>>& vectorSpots )
{
    // we inject an OMP pragma before each outer-most parallel loop
    set<uint32_t> lineInjected;
    for( const auto& spot : parallelSpots )
    {
        if( spot->getParents().empty() )
        {
            uint32_t lineNo = getLineNo(spot);
            if( lineNo )
            {
                InjectParallelFor( spot, lineNo );
            }
        }
    }
    // we inject a "#pragma omp simd" at every parallel reduction
    for( const auto& spot : vectorSpots )
    {
        if( spot->getChildren().empty() )
        {
            uint32_t lineNo = getLineNo(spot);
            if( !lineNo )
            {
                spdlog::warn("Could not find a valid line number for parallel cycle "+to_string(spot->getID())+" in Task"+to_string(spot->getTask()->getID()));
            }
            else
            {
                InjectSIMD( spot, lineNo );
            }
        }
        else
        {
            spdlog::warn("Cannot yet vectorize a reduction that is not the child-most loop.");
        }
    }
    // dump annotated source file
    for( const auto& source : fileLines )
    {
        auto start = source.first.find(".");
        string name = source.first.substr(0, start)+".annotated_omp"+source.first.substr(start);
        ofstream newSource(name);
        string fileString = "";
        for( const auto& line : source.second )
        {
            fileString += line + "\n";
        }
        newSource << fileString;
        newSource.close();
    }
}

void Cyclebite::Grammar::PrintDFGs(const set<shared_ptr<Task>>& tasks)
{
    for (const auto& t : tasks)
    {
        set<shared_ptr<Cyclebite::Graph::ControlBlock>, Cyclebite::Graph::p_GNCompare> taskBlocks;
        for( const auto& c : t->getCycles() )
        {
            taskBlocks.insert(c->getBody().begin(), c->getBody().end());
        }
        ofstream DFGDot("DFG_kernel"+to_string(t->getID())+".dot");
        auto dataGraph = Cyclebite::Graph::GenerateBBSubgraphDot(taskBlocks);
        DFGDot << dataGraph << "\n";
        DFGDot.close();
    }
}