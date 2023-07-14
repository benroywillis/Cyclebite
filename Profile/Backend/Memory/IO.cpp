#include "IO.h"
#include "Memory.h"
#include <cstdlib>
#include <fstream>
#include "Util/Exceptions.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "Epoch.h"
#include "Processing.h"
#include "Kernel.h"
#include <deque>
#include <iomanip>

using namespace std;
using json = nlohmann::json;

namespace Cyclebite::Profile::Backend::Memory
{
    void ReadKernelFile()
    {
        const char *kfName = getenv("KERNEL_FILE");
        if (!kfName)
        {
            kfName = &"kernel.json"[0];
        }
        std::ifstream inputJson;
        json j;
        try
        {
            inputJson.open(kfName);
            inputJson >> j;
            inputJson.close();
        }
        catch (std::exception &e)
        {
            throw AtlasException("Couldn't open kernel file: " + string(kfName) + ": " + string(e.what()));
        }
        if (j.find("Kernels") != j.end())
        {
            // first build all kernel objects and add them to the list
            for (const auto &kid : j["Kernels"].items())
            {
                // filter: if the kernel has multiple children and no parents, therefore we don't pay attention to it
                if( (j["Kernels"][kid.key()]["Parents"].empty()) && (j["Kernels"][kid.key()]["Children"].size() > 1) )
                {
                    // skip because it groups together kernels that are likely just a comprehension over an input data set
                    // but before we skip, add its blocks to the nonkernel code
                    //auto kBlocks = j["Kernels"][kid.key()]["Blocks"].get<set<int64_t>>();
                    //nonKernelBlocks.insert(kBlocks.begin(), kBlocks.end());
                    //spdlog::info("Found a comprehension kernel that was taken out");
                    //continue;
                }
                if( j["Kernels"][kid.key()]["Blocks"].empty() )
                {
                    spdlog::info("Found an empty kernel");
                    continue;
                }
                auto blocks = j["Kernels"][kid.key()]["Blocks"].get<set<int64_t>>();
                auto entrances = map<int64_t, set<int64_t>>();
                auto exits = map<int64_t, set<int64_t>>();
                for (const auto &entid : j["Kernels"][kid.key()]["Entrances"].items())
                {
                    for (const auto &entry : entid.value().get<vector<string>>())
                    {
                        entrances[stol(entid.key())].insert(stol(entry));
                    }
                }
                for (const auto &exid : j["Kernels"][kid.key()]["Exits"].items())
                {
                    for (const auto &entry : exid.value().get<vector<string>>())
                    {
                        exits[stol(exid.key())].insert(stol(entry));
                    }
                }
                auto newKernel = make_shared<Kernel>(blocks, entrances, exits, stoi(kid.key()));
                newKernel->label = j["Kernels"][kid.key()]["Labels"].front();
                kernels.insert(newKernel);
            }
            // now build out the hierarchy structures
            for (const auto &kid : j["Kernels"].items())
            {
                shared_ptr<Kernel> kern = nullptr;
                for( const auto& k : kernels )
                {
                    if( k->kid == stoi(kid.key()) )
                    {
                        kern = k;
                        break;
                    }
                }
                if( !kern )
                {
                    // ineligible kernel, continue
                    continue;
                }
                for (const auto &pid : j["Kernels"][kid.key()]["Parents"])
                {
                    shared_ptr<Kernel> p = nullptr;
                    for( const auto& k : kernels )
                    {
                        if( k->kid == pid )
                        {
                            kern->parents.insert(k);
                            break;
                        }
                    }
                }
                for (const auto &cid : j["Kernels"][kid.key()]["Children"])
                {
                    shared_ptr<Kernel> c = nullptr;
                    for( const auto& k : kernels )
                    {
                        if( k->kid == cid )
                        {
                            kern->children.insert(k);
                            break;
                        }
                    }
                }
            }
            // assign context level
            for (auto kern : kernels)
            {
                if (kern->contextLevel > -1)
                {
                    continue;
                }
                if (kern->parents.empty())
                {
                    kern->contextLevel = 0;
                    continue;
                }
                deque<shared_ptr<Kernel>> Q;
                Q.push_front(kern);
                while (!Q.empty())
                {
                    for (auto p : Q.back()->parents)
                    {
                        if (p->parents.empty())
                        {
                            p->contextLevel = 0;
                            Q.back()->contextLevel = 1;
                            Q.pop_back();
                            break;
                        }
                        else if (p->contextLevel > -1)
                        {
                            Q.back()->contextLevel = p->contextLevel + 1;
                            Q.pop_back();
                            break;
                        }
                        else
                        {
                            Q.push_back(p);
                        }
                    }
                }
            }
        }
        std::map<std::shared_ptr<CodeSection>, std::set<std::shared_ptr<Kernel>, UIDCompare>, UIDCompare> sectionToKernel;
        // construct sectionToBlock map, which maps each kernel to a set of all other kernels that have the same block set
        for( const auto& k : kernels )
        {
            for( const auto& ok : kernels )
            {
                if( k == ok )
                {
                    sectionToKernel[k].insert(ok);
                }
                else
                {
                    set<int64_t> overlap;
                    set<int64_t> un;
                    set_intersection(k->blocks.begin(), k->blocks.end(), ok->blocks.begin(), ok->blocks.end(), std::inserter(overlap, overlap.begin()));
                    set_union(k->blocks.begin(), k->blocks.end(), ok->blocks.begin(), ok->blocks.end(), std::inserter(un, un.begin()));
                    if( (overlap.size() == un.size()) && !(k->blocks.empty()) )
                    {
                        // these kernels have the same block set
                        sectionToKernel[k].insert(ok);
                        sectionToKernel[ok].insert(k);
                    }
                }
            }
        }
        set<shared_ptr<Kernel>, UIDCompare> removed;
        // for each key in the sectionToKernel map, if it is not in the removed set add it to the array, and integrate all information of adjacent block sets (if any)
        for( const auto& bs : sectionToKernel )
        {
            if( removed.find(bs.first) == removed.end() )
            {
                // we have to integrate the hierarchy information from all the different versions of a kernel into this entry
                auto newBS = static_pointer_cast<Kernel>(bs.first);
                for( const auto& sub : bs.second )
                {
                    newBS->children.insert(sub->children.begin(), sub->children.end());
                    newBS->parents.insert(sub->parents.begin(), sub->parents.end());
                }
                blockSets.insert(newBS);
                removed.insert(bs.second.begin(), bs.second.end());
            }
        }
#ifdef DEBUG
        for( const auto& bs : blockSets )
        {
            for( const auto& other : blockSets )
            {
                if( bs == other )
                {
                    continue;
                }
                set<int64_t> overlap;
                set<int64_t> un;
                set_intersection(bs->blocks.begin(), bs->blocks.end(), other->blocks.begin(), other->blocks.end(), std::inserter(overlap, overlap.begin()));
                set_union(bs->blocks.begin(), bs->blocks.end(), other->blocks.begin(), other->blocks.end(), std::inserter(un, un.begin()));
                if( (overlap.size() == un.size()) && !(bs->blocks.empty()) )
                {
                    spdlog::critical("Found a block set that has exactly the same blocks as another block set!");
                    std::exit(EXIT_FAILURE);
                }
            }
        }
#endif
        // insert blocksets into codesections. Codesections will hold all the nonkernels discovered as well
        for (const auto &i : blockSets)
        {
            codeSections.insert(i);
        }

        // construct kernelepochs
        // a kernel epoch is a collection of all blocks in a given hierarchy
        for( const auto& kern : kernels )
        {
            if( kern->contextLevel == 0 )
            {
                // collect all of its child kernel basic blocks into a single set
                set<int64_t> epochBlocks;
                deque<shared_ptr<Kernel>> Q;
                set<shared_ptr<Kernel>, UIDCompare> covered;
                Q.push_front(kern);
                covered.insert(kern);
                while( !Q.empty() )
                {
                    epochBlocks.insert(Q.front()->blocks.begin(), Q.front()->blocks.end());
                    for( const auto& child : Q.front()->children )
                    {
                        if( covered.find(child) == covered.end() )
                        {
                            Q.push_back(child);
                            covered.insert(child);
                        }
                    }
                    Q.pop_front();
                }
                taskCandidates[kern->IID] = epochBlocks;
            }
        }

        // nonkernel blocks will be used to find critical edges in the program
        auto allNKBs = j["NonKernelBlocks"].get<set<int64_t>>();
    }

    string GenerateInstanceDot()
    {
        string dotString = "digraph {\n";
        // label each node after its kernel
        for( const auto& instance : epochs )
        {
            string label = "\"";
            if( instance->kernel ) 
            {
                if( instance->kernel->label.empty() )
                {
                    label += to_string(instance->kernel->kid);
                }
                else
                {
                    label += instance->kernel->label;
                }
                label += ",";
                label += to_string(instance->getMaxFreq())+"\"";
            }
            else
            {
                label += to_string(instance->IID)+","+to_string(instance->getMaxFreq())+"\"";
            }
            if( instance->getMaxFreq() >= MIN_EPOCH_FREQ )
            {
                dotString += "\t"+to_string(instance->IID)+" [label="+label+",color=blue,style=dashed];\n";
            }
            else 
            {
                dotString += "\t"+to_string(instance->IID)+" [label="+label+"];\n";
            }
        }
        // go through the generated array of kernel instances and turn it into a DAG
        auto epochList = vector<shared_ptr<Epoch>>();//epochs.begin(), epochs.end());
        for( const auto& epoch : epochs )
        {
            epochList.push_back(epoch);
        }
        for( auto epoch = epochList.begin(); epoch < prev(epochList.end()); epoch++ )
        {
            // make a solid edge between the current time point and the next
            auto currentEpoch = *epoch;
            auto nextEpoch = *next(epoch);
            dotString += "\t"+to_string(currentEpoch->IID)+" -> "+to_string(nextEpoch->IID)+" [style=solid];\n";
        }
        dotString += "}";
        ofstream dotOutput;
        auto dotFileName = getenv("MEMORY_DOTFILE");
        if( dotFileName )
        {
            dotOutput = ofstream(dotFileName);
        }
        else
        {
            dotOutput = ofstream("DAG.dot");
        }
        dotOutput << dotString;
        dotOutput.close();
        return dotString;
    }

    string GenerateTaskOnlyInstanceDot()
    {
        string dotString = "digraph {\n";
        // label each node after its kernel
        for( const auto& instance : epochs )
        {
            if( instance->kernel )
            {
                string kLabel = "\"";
                if( !instance->kernel->label.empty() )
                {
                    kLabel += instance->kernel->label;
                }
                else
                {
                    kLabel += to_string(instance->kernel->kid);
                }
                kLabel += ",";
                kLabel += to_string(instance->getMaxFreq())+"\"";
                dotString += "\t"+to_string(instance->IID)+" [label="+kLabel+",color=blue,style=dashed];\n";
            }
        }
        // go through the generated array of kernel instances and turn it into a DAG
        auto epochList = vector<shared_ptr<Epoch>>();//epochs.begin(), epochs.end());
        for( const auto& epoch : epochs )
        {
            if( epoch->kernel )
            {
                epochList.push_back(epoch);
            }
        }
        if( !epochList.empty() )
        {
            for( auto epoch = epochList.begin(); epoch < prev(epochList.end()); epoch++ )
            {
                // make a solid edge between the current time point and the next
                auto currentEpoch = *epoch;
                auto nextEpoch = *next(epoch);
                dotString += "\t"+to_string(currentEpoch->IID)+" -> "+to_string(nextEpoch->IID)+" [style=solid];\n";
            }
        }
        else
        {
            spdlog::warn("No epochs detected");
        }
        dotString += "}";
        ofstream dotOutput;
        auto dotFileName = getenv("MEMORY_DOTFILE");
        if( dotFileName )
        {
            dotOutput = ofstream(dotFileName);
        }
        else
        {
            dotOutput = ofstream("DAG.dot");
        }
        dotOutput << dotString;
        dotOutput.close();
        return dotString;
    }

    void GenerateTaskGraph()
    {
        auto taskComms = GenerateTaskCommunication();
        auto DAG = GenerateInstanceDot();
        // remove the closing brace from the DAG string
        DAG.pop_back();
        // now simply add dotted edges to denote RAW and WAW dependencies
        for( const auto& task : taskComms )
        {
            for( const auto& producer : task.second.first )
            {
                DAG += "\t"+to_string(task.first)+" -> "+to_string(producer)+" [label=\"RAW\",style=dotted];\n";
            }
            for( const auto& producer : task.second.second )
            {
                DAG += "\t"+to_string(task.first)+" -> "+to_string(producer)+" [label=\"WAW\",style=dotted];\n";
            }
        }
        DAG += "}";
        ofstream dotOutput;
        auto dotFileName = getenv("TASKGRAPH_FILE");
        if( dotFileName )
        {
            dotOutput = ofstream(dotFileName);
        }
        else
        {
            dotOutput = ofstream("TaskGraph.dot");
        }
        dotOutput << DAG;
        dotOutput.close();
    }

    void GenerateTaskOnlyTaskGraph()
    {
        auto taskComms = GenerateTaskCommunication();
        auto DAG = GenerateTaskOnlyInstanceDot();
        // remove the closing brace from the DAG string
        DAG.pop_back();
        // now simply add dotted edges to denote RAW and WAW dependencies
        for( const auto& task : taskComms )
        {
            auto epoch = *epochs.find(task.first);
            if( epoch )
            {
                if( epoch->kernel )
                {
                    for( const auto& producer : task.second.first )
                    {
                        auto producerEpoch = *epochs.find(producer);
                        if( producerEpoch )
                        {
                            if( producerEpoch->kernel )
                            {
                                DAG += "\t"+to_string(task.first)+" -> "+to_string(producer)+" [label=\"RAW\",style=dotted];\n";
                            }
                        }
                    }
                    /*for( const auto& producer : task.second.second )
                    {
                        DAG += "\t"+to_string(task.first)+" -> "+to_string(producer)+" [label=\"WAW\",style=dotted];\n";
                    }*/
                }
            }
        }
        DAG += "}";
        ofstream dotOutput;
        auto dotFileName = getenv("TASKGRAPH_FILE");
        if( dotFileName )
        {
            string fileName = string(dotFileName) + string("_taskonly");
            dotOutput = ofstream(fileName);
        }
        else
        {
            dotOutput = ofstream("TaskGraph_TASKONLY.dot");
        }
        dotOutput << DAG;
        dotOutput.close();
    }

    void OutputKernelInstances()
    {
        // take the kernels that were "locally" hot, get the entries from the input kernel file that match those kernels and output them in a new "kernel instance" json
        set<shared_ptr<Kernel>, UIDCompare> hotKernels;
        uint64_t hotInstances = 0;
        for( const auto& epoch : epochs )
        {
            if( epoch->kernel )
            {
                if( epoch->getMaxFreq() > MIN_EPOCH_FREQ )
                {
                    hotInstances++;
                    hotKernels.insert(epoch->kernel);
                }
            }
        }

        spdlog::info("Found "+to_string(hotInstances)+" hot kernel instances.");
        spdlog::info("Found "+to_string(hotKernels.size())+" unique kernel instances.");

        const char *kfName = getenv("KERNEL_FILE");
        if (!kfName)
        {
            kfName = &"kernel.json"[0];
        }
        std::ifstream inputJson;
        json input;
        try
        {
            inputJson.open(kfName);
            inputJson >> input;
            inputJson.close();
        }
        catch (std::exception &e)
        {
            throw AtlasException("Couldn't open kernel file: " + string(kfName) + ": " + string(e.what()));
        }
        json output;
        for( const auto& k : hotKernels )
        {
            if( input["Kernels"].find( to_string(k->kid) ) != input["Kernels"].end() )
            {
                output["Kernels"][ to_string(k->kid) ] = input["Kernels"][ to_string(k->kid) ];
            }
        }
        output["Average Kernel Size (Blocks)"] = input["AVerage Kernel Size (Blocks)"];
        output["Average Kernel Size (Nodes)"] = input["Average Kernel Size (Nodes)"];
        output["Entropy"] = input["Entropy"];
        output["BlockCallers"] = input["BlockCallers"];
        output["NonKernelBlocks"] = input["NonKernelBlocks"];
        output["ValidBlocks"] = input["ValidBlocks"];

        string OutputFileName = "instance.json";
        if( getenv("INSTANCE_FILE") )
        {
            OutputFileName = string(getenv("INSTANCE_FILE"));
        }
        ofstream oStream(OutputFileName);
        oStream << setw(4) << output;
        oStream.close();
    }
} // namespace Cyclebite::Profile::Backend::Memory