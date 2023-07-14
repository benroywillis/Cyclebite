#include "Util/Exceptions.h"
#include "Kernel.h"
#include "KernelInstance.h"
#include "NonKernel.h"
#include "NonKernelInstance.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#define STACK_SIZE 1000

using namespace std;
using json = nlohmann::json;

/* John 5/26/2022
 * Using epochs to trace kernel instances
 *  - use kernel entrances/exits to increment the epoch, mark them in the instrumented profile
 *  - use function inlining to "tag" function inlines to disambiguate kernels that overlap
 * Data tuples
 *  - avoid writing huge arrays
 *  - prior work didn't necessarily care about task graphs composed of kernels... thus they didn't deal with aliasing in the mem profile...
 * MVP: What are the critical loads and stores, and how much data are they touching?
 * Lessons from LL: a lot of the memory stuff is in the LLVM bitcode spec... so we just have to build annotators for those things
 */

namespace Cyclebite::Backend::BackendInstance
{
    /// Holds all CodeSection instances (each member is polymorphic to either kernels or nonkernels)
    set<CodeSection *, p_UIDCompare> codeSections;
    /// Reverse mapping from block to CodeSection
    map<uint64_t, set<CodeSection *, p_UIDCompare>> BlockToSection;
    /// Holds all kernel instances
    set<Kernel *, p_UIDCompare> kernels;
    // Holds all nonkernel instances
    set<NonKernel *, p_UIDCompare> nonKernels;
    /// Holds all kernels that are currently alive
    //std::set<CodeSection *, p_UIDCompare> liveKernels;
    /// Holds the order of kernel instances measured while profiling (kernel IDs, instance index)
    std::vector<pair<int, int>> TimeLine;
    /// Current non kernel instance. If nullptr there is no current non-kernel instance
    NonKernelInstance *currentNKI;
    /// Remembers the block seen before the current so we can dynamically find kernel exits
    uint64_t lastBlock;

    /// On/off switch for the profiler
    bool instanceActive = false;

    void GenerateDot(const set<CodeSection *, p_UIDCompare> &codeSections)
    {
        if (TimeLine.empty())
        {
            cout << "Timeline empty! No dot file to produce" << endl;
            return;
        }
        string dotString = "digraph{\n";
        // label kernels and nonkernels
        for (const auto &cs : codeSections)
        {
            if (auto kernel = dynamic_cast<Kernel *>(cs))
            {
                for (const auto &instance : kernel->getInstances())
                {
                    dotString += "\t" + to_string(instance->IID) + " [label=\"" + kernel->label + "\"]\n";
                }
            }
            else if (auto nk = dynamic_cast<NonKernel *>(cs))
            {
                string nkLabel = "";
                auto blockIt = nk->blocks.begin();
                nkLabel += to_string(*blockIt);
                blockIt++;
                for (; blockIt != nk->blocks.end(); blockIt++)
                {
                    nkLabel += "," + to_string(*blockIt);
                }
                for (const auto &instance : nk->getInstances())
                {
                    dotString += "\t" + to_string(instance->IID) + " [label=\"" + nkLabel + "\"]\n";
                }
            }
            else
            {
                throw AtlasException("CodeSection mapped to neither a Kernel nor a Nonkernel!");
            }
        }
        // now build out the nodes in the graph
        // go through all kernel hierarchies that execute sequentially and build the solid lines (temporal relationships) and dotted lines (hierarchical relationships) that connect them
        // we only go through the second to last hierarchy because the last one has no outgoing temporal edges
        for (unsigned int i = 0; i < TimeLine.size() - 1; i++)
        {
            auto currentSectionIt = codeSections.find(TimeLine[i].first);
            if (currentSectionIt == codeSections.end())
            {
                throw AtlasException("currentSection in the timeline does not map to an existing code section!");
            }
            auto currentSection = *currentSectionIt;
            auto nextSectionIt = codeSections.find(TimeLine[i + 1].first);
            if (nextSectionIt == codeSections.end())
            {
                throw AtlasException("currentSection in the timeline does not map to an existing code section!");
            }
            auto nextSection = *nextSectionIt;
            // currentInstance holds the object of the current codeSection
            CodeInstance *currentInstance;
            // encode the hierarchy for the current node if this is a kernel hierarchy, else graph and encode the nonkernel codesection
            if (auto currentKernel = dynamic_cast<Kernel *>(currentSection))
            {
                // queue for recursing through the hierarchy
                std::deque<KernelInstance *> Q;
                // list that builds out all instances on a given context level (breadth search)
                vector<KernelInstance *> hierarchy;
                Q.push_front(currentKernel->getInstance((unsigned int)TimeLine[i].second));
                while (!Q.empty())
                {
                    for (auto child = Q.front()->children.begin(); child != Q.front()->children.end(); child++)
                    {
                        Q.push_back(*child);
                    }
                    hierarchy.push_back(Q.front());
                    Q.pop_front();
                }
                for (auto UID = next(hierarchy.begin()); UID != hierarchy.end(); UID++)
                {
                    dotString += "\t" + to_string((*UID)->IID) + " -> " + to_string((*prev(UID))->IID) + " [style=dashed] [label=" + to_string((*UID)->iterations) + "];\n";
                }
                currentInstance = currentKernel->getInstance((unsigned int)TimeLine[i].second);
            }
            else if (auto currentNonKernel = dynamic_cast<NonKernel *>(currentSection))
            {
                // nonkernel sections by definition cannot have more than 1 iteration
                currentInstance = currentNonKernel->getCurrentInstance();
            }
            else
            {
                throw AtlasException("currentSection casts to neither a kernel nor a non-kernel!");
            }
            // nextInstance holds the object of the codesection that happens (temporally) after the current
            //CodeInstance *nextInstance;
            // now encode the edge connecting this kernel instance with whatever comes next
            /*if (auto nextKernel = dynamic_cast<Kernel *>(nextSection))
            {
                nextInstance = nextKernel->getInstance((unsigned int)TimeLine[i+1].second);
            }
            else if (auto nextNonKernel = dynamic_cast<NonKernel *>(nextSection))
            {
                nextInstance = nextNonKernel->getInstance((unsigned int)TimeLine[i+1].second);
            }
            else
            {
                throw AtlasException("nextSection maps to neither a kernel nor a non-kernel!");
            }*/
            auto nextInstance = nextSection->getCurrentInstance();
            // TODO: add iteration count to the edge
            dotString += "\t" + to_string(currentInstance->IID) + " -> " + to_string(nextInstance->IID) + ";\n"; //[label=" + to_string(currentSection->) + "];\n";
        }
        dotString += "}";

        // print file
        ofstream DAGStream("DAG.dot");
        DAGStream << dotString << "\n";
        DAGStream.close();
    }

    void PushNonKernel(uint64_t currentBlock)
    {
        if (currentNKI)
        {
            // first we have to find out whether or not this nonKernel has been seen before
            // go through all non-kernel instances and find one whose block set matches this one
            NonKernel *match = nullptr;
            for (auto nk : nonKernels)
            {
                if (nk->blocks == currentNKI->blocks)
                {
                    match = nk;
                    break;
                }
            }
            if (!match)
            {
                match = new NonKernel();
                codeSections.insert(match);
                nonKernels.insert(match);
            }
            // mark this non-kernel instance in the timeline
            TimeLine.push_back(pair<int, int>(match->IID, match->getInstances().size()));
            // look to see if we have an instance exactly like this one already in the nonkernel
            for (auto instance : match->getInstances())
            {
                if (instance->firstBlock == currentNKI->firstBlock)
                {
                    // we already have this instance
                    delete currentNKI;
                    instance->iterations++;
                    currentNKI = nullptr;
                }
            }
            if (currentNKI != nullptr)
            {
                currentNKI->nk = match;
                // push the new instance
                match->addInstance(currentNKI);
                match->blocks.insert(currentNKI->blocks.begin(), currentNKI->blocks.end());
                match->entrances[(int64_t)lastBlock].push_back((int64_t)currentBlock);
            }
            for (const auto &b : match->blocks)
            {
                BlockToSection[(uint64_t)b].insert(match);
            }
            currentNKI = nullptr;
        }
    }

    extern "C"
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
                cout << "Critical: Couldn't open kernel file: " + string(kfName) << endl;
                cout << e.what() << endl;
                exit(EXIT_FAILURE);
            }
            if (j.find("Kernels") != j.end())
            {
                // first build all kernel objects and add them to the list
                for (const auto &kid : j["Kernels"].items())
                {
                    auto newKernel = new Kernel(stoi(kid.key()));
                    newKernel->label = j["Kernels"][kid.key()]["Labels"].front();
                    newKernel->blocks.insert(j["Kernels"][kid.key()]["Blocks"].begin(), j["Kernels"][kid.key()]["Blocks"].end());
                    kernels.insert(newKernel);
                }
                // now build out the hierarchy structures
                for (const auto &kid : j["Kernels"].items())
                {
                    auto kern = *kernels.find(stoi(kid.key()));
                    auto parents = j["Kernels"][kid.key()]["Parents"];
                    auto children = j["Kernels"][kid.key()]["Children"];
                    for (const auto &pid : parents)
                    {
                        kern->parents.insert(*kernels.find(pid));
                    }
                    for (const auto &cid : children)
                    {
                        kern->children.insert(*kernels.find(cid));
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
                    deque<Kernel *> Q;
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
            // insert modified kernels into codeSections so they can be sorted properly
            for (const auto &i : kernels)
            {
                codeSections.insert(i);
            }
            for (const auto k : kernels)
            {
                for (const auto &b : k->blocks)
                {
                    BlockToSection[(uint64_t)b].insert(k);
                }
            }
        }

        void InstanceDestroy()
        {
            // first check if there is a non-kernel in progress and store it
            PushNonKernel(lastBlock);
            // output data structure
            // first create an instance to kernel ID mapping
            // remember that this is hierarchical
            map<uint32_t, vector<vector<UniqueID *>>> TimeToInstances;
            json instanceMap;
            for (uint32_t i = 0; i < TimeLine.size(); i++)
            {
                auto currentSegment = codeSections.find(TimeLine[i].first);
                if (currentSegment == codeSections.end())
                {
                    throw AtlasException("The current timeline entry does not map to an existing code segment!");
                }
                if (auto currentKernel = dynamic_cast<Kernel *>(*currentSegment))
                {
                    // construct a map for all embedded kernel instances in a given hierarchical instance
                    std::deque<vector<KernelInstance *>> Q;
                    // hierarchy of the current kernel instance. Front element is the parent-most kernel
                    vector<vector<UniqueID *>> hierarchy;
                    // the TimeLine hands us the top-level kernel. Thus we just need to follow where its children lead and we will acquire the entire hierarchy in order
                    vector<KernelInstance *> start;
                    start.push_back(currentKernel->getInstance((unsigned int)TimeLine[i].second));
                    Q.push_front(start);
                    while (!Q.empty())
                    {
                        vector<KernelInstance *> tmp;
                        for (auto entry : Q.front())
                        {
                            for (auto child = entry->children.begin(); child != entry->children.end(); child++)
                            {
                                tmp.push_back(*child);
                            }
                        }
                        if (!tmp.empty())
                        {
                            Q.push_back(tmp);
                        }
                        hierarchy.push_back(vector<UniqueID *>(Q.front().begin(), Q.front().end()));
                        Q.pop_front();
                    }
                    TimeToInstances[i] = hierarchy;
                }
                else if (auto currentNonKernel = dynamic_cast<NonKernel *>(*currentSegment))
                {
                    TimeToInstances[i] = vector<vector<UniqueID *>>();
                    vector<UniqueID *> instances;
                    for (const auto &i : currentNonKernel->getInstances())
                    {
                        instances.push_back(i);
                    }
                    TimeToInstances[i].push_back(instances);
                }
                else
                {
                    throw AtlasException("ID in the TimeLine mapped to neither a kernel nor a nonkernel!");
                }
            }

            // output what happens at each time instance
            for (const auto &time : TimeToInstances)
            {
                instanceMap["Time"][to_string(time.first)] = vector<vector<pair<int, int>>>();
                for (auto level : time.second)
                {
                    vector<pair<int, int>> newLevel;
                    for (auto UID : level)
                    {
                        if (auto instance = dynamic_cast<KernelInstance *>(UID))
                        {
                            newLevel.push_back(pair<int, int>(instance->k->IID, instance->iterations));
                        }
                        else if (auto nkinstance = dynamic_cast<NonKernelInstance *>(UID))
                        {
                            newLevel.push_back(pair<int, int>(nkinstance->nk->IID, nkinstance->iterations));
                        }
                    }
                    instanceMap["Time"][to_string(time.first)].push_back(newLevel);
                }
            }

            // output kernel instances for completeness and non-kernel instances for reference
            for (const auto &cs : codeSections)
            {
                if (auto k = dynamic_cast<Kernel *>(cs))
                {
                    instanceMap["Kernels"][to_string(k->IID)]["Blocks"] = vector<uint64_t>(k->blocks.begin(), k->blocks.end());
                    instanceMap["Kernels"][to_string(k->IID)]["Entrances"] = k->entrances;
                    instanceMap["Kernels"][to_string(k->IID)]["Exits"] = k->exits;
                    instanceMap["Kernels"][to_string(k->IID)]["Parents"] = vector<uint64_t>();
                    for (const auto &p : k->parents)
                    {
                        instanceMap["Kernels"][to_string(k->IID)]["Parents"].push_back(p->IID);
                    }
                    instanceMap["Kernels"][to_string(k->IID)]["Children"] = vector<uint64_t>();
                    for (const auto &p : k->children)
                    {
                        instanceMap["Kernels"][to_string(k->IID)]["Children"].push_back(p->IID);
                    }
                }
                else if (auto nk = dynamic_cast<NonKernel *>(cs))
                {
                    instanceMap["NonKernels"][to_string(nk->IID)]["Blocks"] = vector<uint64_t>(nk->blocks.begin(), nk->blocks.end());
                    instanceMap["NonKernels"][to_string(nk->IID)]["Entrances"] = nk->entrances;
                    instanceMap["NonKernels"][to_string(nk->IID)]["Exits"] = nk->exits;
                }
                else
                {
                    throw AtlasException("CodeSegment pointer is neither a kernel nor a nonkernel!");
                }
            }

            // print the file
            ofstream file;
            char *instanceFileName = getenv("INSTANCE_FILE");
            if (instanceFileName == nullptr)
            {
                file.open("Instance.json");
            }
            else
            {
                file.open(instanceFileName);
            }
            file << setw(4) << instanceMap;

            // generate dot file for the DAG
            GenerateDot(codeSections);

            // free our stuff
            for (auto entry : codeSections)
            {
                for (auto instance : entry->getInstances())
                {
                    delete instance;
                }
                delete entry;
            }
            instanceActive = false;
        }

        void InstanceIncrement(uint64_t a)
        {
            if (!instanceActive)
            {
                return;
            }
            // first, handle all codesections that were live in the last block and are no longer live
            // XOR the current block sections from the previous block sections to find all sections that have either been entered or exited
            set<CodeSection *, p_UIDCompare> XOR;
            set_symmetric_difference(BlockToSection[a].begin(), BlockToSection[a].end(), BlockToSection[lastBlock].begin(), BlockToSection[lastBlock].end(), std::inserter(XOR, XOR.begin()), p_UIDCompare());
            // the intersection of the previous block sections and the current block sections are continuing to execute
            set<CodeSection *, p_UIDCompare> continuingSections;
            set_intersection(BlockToSection[a].begin(), BlockToSection[a].end(), BlockToSection[lastBlock].begin(), BlockToSection[lastBlock].end(), std::inserter(continuingSections, continuingSections.begin()), p_UIDCompare());
            // entered sections are the sections in the XOR that are not in the prior block sections
            set<CodeSection *, p_UIDCompare> enteredSections;
            set_difference(XOR.begin(), XOR.end(), BlockToSection[lastBlock].begin(), BlockToSection[lastBlock].end(), std::inserter(enteredSections, enteredSections.begin()), p_UIDCompare());
            // exited sections are the sections in the XOR that are not in the current block sections
            set<CodeSection *, p_UIDCompare> exitedSections;
            set_difference(XOR.begin(), XOR.end(), BlockToSection[a].begin(), BlockToSection[a].end(), std::inserter(exitedSections, exitedSections.begin()), p_UIDCompare());

            set<Kernel *, p_UIDCompare> liveKernels;
            for (auto sec : continuingSections)
            {
                if (auto kern = dynamic_cast<Kernel *>(sec))
                {
                    liveKernels.insert(kern);
                    if (std::find(kern->entrances[(int64_t)lastBlock].begin(), kern->entrances[(int64_t)lastBlock].end(), (int64_t)a) != kern->entrances[(int64_t)lastBlock].end())
                    {
                        // this kernel was already live and we have seen its entrance without exiting
                        // this constitutes a revolution, so update its iterations
                        kern->getCurrentInstance()->iterations++;
#if DEBUG
                        if (kern->entrances.size > 1)
                        {
                            spdlog::warn("Incrementing iteration count for a kernel that has more than one entrance");
                        }
#endif
                    }
                }
            }
            for (auto exited : exitedSections)
            {
                // this is an exit for this section
                exited->exits[(int64_t)lastBlock].insert(exited->exits[(int64_t)lastBlock].cbegin(), (int64_t)a);
                if (auto kern = dynamic_cast<Kernel *>(exited))
                {
                    liveKernels.erase(kern);
                }
            }
            set<Kernel *, HierarchySort> enteredKernels;
            for (auto cs : enteredSections)
            {
                if (auto kern = dynamic_cast<Kernel *>(cs))
                {
                    enteredKernels.insert(kern);
                    liveKernels.insert(kern);
                    kern->entrances[(int64_t)lastBlock].insert(kern->entrances[(int64_t)lastBlock].cbegin(), (int64_t)a);
                    // take care of any serial code that occurred before our kernel
                    PushNonKernel(a);
                }
            }

            // TODO: the below code assumes that there cannot be back-to-back kernel instances in a hierarchical level that is not the main context level (level 0)
            // right now, the semantic is to create a new instance for a child if the parentinstance doesn't have one yet, thus we can only have one child instance for each parent instance

            // now make new kernel instances if necessary
            // we must go top-down in the hierarchy when creating instances aka parents must go first
            for (auto enteredKern : enteredKernels)
            {
                // instances are in the eye of the parent (we only consider an instance new from the perspective of the hierarchical level immediately above us)
                if (enteredKern->parents.empty())
                {
                    // must insert to the map before making a new instance, the instance size needs to be recorded before we make a new one
                    TimeLine.push_back(pair<int, int>(enteredKern->IID, enteredKern->getInstances().size()));
                    new KernelInstance(enteredKern);
                }
                else if (enteredKern->parents.size() == 1)
                {
                    // This is the semantic that prevents the current method from detecting multiple kernel instances on the same context level
                    // By restricting the tool to only make one instance for a child, we save a lot of room and make the space of kernel instances much smaller
                    // if we already have an instance for this child in the parent we don't make a new one
                    auto parent = *(enteredKern->parents.begin());
                    auto parentInstance = parent->getCurrentInstance();
                    if (!parentInstance)
                    {
                        throw AtlasException("Found a parent kernel that does not have an instance before its child!");
                    }
                    bool childFound = false;
                    // TODO: this loop needs to be turned into a search method for the children set
                    for (auto child : parentInstance->children)
                    {
                        if (child->k == enteredKern)
                        {
                            childFound = true;
                        }
                    }
                    if (!childFound)
                    {
                        // we don't have an instance for this child yet, create one
                        auto newInstance = new KernelInstance(enteredKern);
                        parentInstance->children.insert(newInstance);
                    }
                }
                else
                {
                    throw AtlasException("Don't know what to do about finding the current kernel instance when there is more than one parent!");
                }
            }
            // if we don't find any live kernels it means we are in non-kernel code
            if (liveKernels.empty())
            {
                if (currentNKI)
                {
                    currentNKI->blocks.insert((int64_t)a);
                }
                else
                {
                    currentNKI = new NonKernelInstance((int64_t)a);
                    currentNKI->firstBlock = (int64_t)a;
                }
            }
            lastBlock = a;
        }

        void InstanceInit(uint64_t a)
        {
            ReadKernelFile();
            instanceActive = true;
            lastBlock = a;
            InstanceIncrement(a);
        }
    }
} // namespace Cyclebite::Backend::BackendInstance