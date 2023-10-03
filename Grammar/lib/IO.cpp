#include "IO.h"
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