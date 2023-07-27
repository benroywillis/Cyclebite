#include "IO.h"
#include "Graph/inc/IO.h"
#include "Util/Exceptions.h"
#include <spdlog/spdlog.h>

using namespace std;
using namespace Cyclebite::Grammar;

set<shared_ptr<Cyclebite::Graph::DataNode>, Cyclebite::Graph::p_GNCompare> Cyclebite::Grammar::SignificantMemInst;

void Cyclebite::Grammar::InjectSignificantMemoryInstructions(const nlohmann::json& instanceJson, const map<int64_t, llvm::Value*>& IDToValue)
{
    if( instanceJson.find("Instruction Tuples") == instanceJson.end() )
    {
        spdlog::critical("Could not find 'Instruction Tuples' category in input instance file!");
        throw AtlasException("Could not find 'Instruction Tuples' category in input instance file!");
    }
    for( const auto& value : instanceJson["Instruction Tuples"].items() )
    {
        auto val = IDToValue.at(stol(value.key()));
        if( val )
        {
            if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(val) )
            {
                // mark as significant
                SignificantMemInst.insert( Cyclebite::Graph::DNIDMap.at(inst) );
            }
        }
    }
}