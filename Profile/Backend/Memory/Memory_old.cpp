#include "AtlasUtil/Exceptions.h"
#include "AtlasUtil/IO.h"
#include "Kernel.h"
#include "KernelInstance.h"
#include "NonKernel.h"
#include "NonKernelInstance.h"
#include "Iteration.h"
#include <cstdlib>
#include <iomanip>
#include <queue>

using namespace std;
using json = nlohmann::json;
using namespace llvm;

/// On/off switch for nonkernel evaluations
#define NONKERNEL 1

/// On/off switch for kernel hierarchy evaluations
/// Hierarchy evaluations treat each individual kernel as a separate entity
/// Turning this off makes the entire hierarchy of more than one kernel a single entity
#define HIERARCHY 0

/* John [6/10/22]: advice on how to scale the nonkernel code to hard cases
* we know the answer to the question "is a basic block a kernel, nonkernel, or both?" in advance
* we can figure out when we enter and exit a kernel dynamically (because we crossed a boundary edge)
* assumption: if a shared function is both kernel and non kernel, it cannot be the entrance or exit to a kernel. because if it is the entrance, it would be a kernel with a single instance (the single call to the function)
* approach: edges that both belong to kernels and nonkernels do not get to form boundaries (this will likely hold up given the transformations to the static bitcode)
* as we profile, just ask "are you the edge to a kernel?" or "are you the edge from a kernel?" and the answer to either of those will get us all we need
*/

/* John 7\15\22
 * the primary reason for the memory pass is to find the produce-consume relationships between kernels, not within kernels
 * 1. what are the data slabs that exist between the tasks? do they have a name? how big are they?
 * 2. are we able to distinguish between many data slabs?
 * 3. are we able to reconstitute what the original data slab was even for random memory accesses?
 * 
 * We need to denoise the data space (just like the control space)
 * - make a distinction between stack tuple and heap tuple
 * - categories
 *   - "early" stack (early in the program only, everything else is perhaps not interesting)
 *   - "late"  stack
 *   - heap (malloc, calloc, etc)
 * 
 */
namespace TraceAtlas::Profile::Backend
{
    /// Thresholds
    /// Minimum offset a memory tuple must have (in bytes) to be considered for memory prod/cons graph 
    constexpr uint32_t MIN_TUPLE_OFFSET = 32;

    /// map IDs to blocks and values
    map<int64_t, BasicBlock *> IDToBlock;
    map<int64_t, Value *> IDToValue;

    /// Maps critical edges to the codesections they enter
    /// Holds edges that transition from one part of the program to the other
    /// These edges are encoded as pairs of block IDs, first entry is the source, second is sink
    map<pair<int64_t, int64_t>, set<shared_ptr<CodeSection>, UIDCompare>> enteringEdges;
    map<pair<int64_t, int64_t>, set<shared_ptr<CodeSection>, UIDCompare>> exitingEdges;

    /// Holds all CodeSections
    /// A code section is a unique set of basic block IDs ie a codesection may map to multiple kernels
    set<shared_ptr<CodeSection>, UIDCompare> codeSections;
    /// Maps a code section to a kernel
    map<shared_ptr<CodeSection>, set<shared_ptr<Kernel>, UIDCompare>, UIDCompare> sectionToKernel;
    /// Reverse mapping from block to CodeSection
    map<int64_t, set<shared_ptr<CodeSection>, UIDCompare>> BlockToSection;
    /// Maps a codesection to the instance number it is currently on. All sections start at 0, where instance 0 means the codesection has not run at all
    map<shared_ptr<CodeSection>, uint64_t> InstanceCount;
    /// Holds all unique block sets in the program
    set<shared_ptr<Kernel>, UIDCompare> blockSets;
    /// Holds all non-kernel codesections
    set<shared_ptr<NonKernel>, UIDCompare> nonKernels;
    /// Maps a kernel to its dominators
    /// Dominators are kernels that must happen before a given kernel can execute
    map<shared_ptr<Kernel>, set<int64_t>> dominators;

    /// Holds all blocks that belong to kernel/nonkernel code sections
    /// These sets may not be mutually exclusive, but they must be collectively exhaustive
    set<int64_t> kernelBlocks;
    set<int64_t> nonKernelBlocks;
    /// A set of block IDs that have already executed been seen in the profile
    set<int64_t> executedBlocks;
    /// Kernels that are currently live. HierarchySort sorts entries from parent-most to child-most and resolves ties with IID
    set<shared_ptr<Kernel>, HierarchySort> liveKernels;
    /// Holds the order of kernel instances measured while profiling (kernel IDs, instance index)
    std::vector<pair<int, int>> TimeLine;
    /// Holds the current kernel instance(s)
    shared_ptr<KernelInstance> currentKI;
    /// Holds the current nonkernel instance
    shared_ptr<NonKernelInstance> currentNKI = nullptr;
    /// Holds the current iteration information
    Iteration currentIteration;
    /// Remembers the block seen before the current so we can dynamically find kernel exits
    int64_t lastBlock;
    /// On/off switch for the profiler
    bool memoryActive = false;

    /// DAGArray is a 3D representation of the DAG
    /// first dimension (outermost): sequence of time, where each entry represents a new "section" of the code (which gets its own timeslot). This "section" can be a kernel hierarchy or nonkernel
    /// second dimension: depth of section. This will be one entry for nonkernels and kernels with no children. It can be multi-level for kernel hierarchies or kernels that have nonkernel loops below them
    /// third dimension: breadth of depth level. This can represent multiple children of the same hierarchy level. For example, a nonkernel loop that has multiple kernels in its hierarchy. Another example: a parent kernel with multiple children
    /// fourth dimension: first entry is the codeiteration ID that belongs there, second entry is the iteration count of that section
    vector<vector<vector<pair<uint64_t, uint64_t>>>> DAGArray;
    /// maps a code section ID to its read/write footprints
    map< uint64_t, pair<set<MemTuple, MTCompare>, set<MemTuple, MTCompare>> > CIFootprints;
    /// maps a time slot to a pair of footprints of memory it writes to/reads from
    map< uint32_t, pair<set<MemTuple, MTCompare>, set<MemTuple, MTCompare>> > hierarchyFootprints;

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
        set<shared_ptr<Kernel>, UIDCompare> kernels;
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
                    spdlog::info("Found a comprehension kernel that was taken out");
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
                kernelBlocks.insert(blocks.begin(), blocks.end());
                // build predicate and successors entry
                //dominators[newKernel] = j["Kernels"][kid.key()]["Dominators"].get<set<int64_t>>();
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
                    exit(EXIT_FAILURE);
                }
            }
        }
#endif
        // insert blocksets into codesections. Codesections will hold all the nonkernels discovered as well
        for (const auto &i : blockSets)
        {
            codeSections.insert(i);
        }
        for (const auto k : blockSets)
        {
            for (const auto &b : k->blocks)
            {
                BlockToSection[b].insert(k);
            }
        }

        // nonkernel blocks will be used to find critical edges in the program
        auto allNKBs = j["NonKernelBlocks"].get<set<int64_t>>();
        nonKernelBlocks.insert(allNKBs.begin(), allNKBs.end());
    }

    /// @brief Parses the entrances and exits of kernels to decide which edges in the graph represent task boundaries
    ///
    /// An epoch is a time interval of the program that is taken by a task instance. A task instance can be a single kernel or kernel hierarchy.
    /// An epoch boundary is a state transition in the program where we can definitively say that a task has been entered or exited
    /// A boundary must satisfy the following rules
    /// 1. Each side of the boundary (the source and sink node) must not be an intersection of the kernel block-nonkernel block sets ie they cannot both be a kernel and nonkernel
    void FindEpochBoundaries()
    {
        // first step, find critical edges for kernel entrances
        for (auto k : blockSets)
        {
#if NONKERNEL == 0
            if( !k->parents.empty() )
            {
                continue;
            }
#endif
            for (auto e : k->entrances)
            {
                for (auto entry : e.second)
                {
                    if (!(kernelBlocks.find(entry) != kernelBlocks.end() && nonKernelBlocks.find(entry) != nonKernelBlocks.end()))
                    {
                        enteringEdges[pair(e.first, entry)].insert(k);
                    }
                    else
                    {
                        throw AtlasException("Kernel entrance sink node intersected both kernel and non-kernel code!");
                    }
                }
            }
            for (auto e : k->exits)
            {
                if (!(kernelBlocks.find(e.first) != kernelBlocks.end() && nonKernelBlocks.find(e.first) != nonKernelBlocks.end()))
                {
                    for (auto entry : e.second)
                    {
                        exitingEdges[pair(e.first, entry)].insert(k);
                    }
                }
                else
                {
                    // exit sink nodes can be shared (unlike entrances) because they go out into the wild of the program.. and thus may be shared among many things
                    // this exception ( if active ) will be thrown in git@github.com:benroywillis/Algorithms/BilateralGrid/AndrewAdams/ project
                    throw AtlasException("Kernel exit source node intersected both kernel and non-kernel code!");
                }
            }
        }
    }

    /// @brief Finds which kernel instance is the correct one after a clash of more than one kernel entrance
    ///
    /// This case arises from function inlining. When functions that are inlined contain kernels, the kernel is duplicated but the underlying blocks are not. When we profile the instances of these kernels, they have the same entrances.
    /// For kernels that have specific parent kernels, the problem becomes finding which parents are alive. This should leave you with only one option out of those available that makes sense
    /// For kernels that have no parents, the nonkernel blocks are used. Inlined functions should have specific non-kernel blocks that have to execute before we get to the inlined callsite, therefore the nonkernels already found should tell us which kernel we are on
    /// @param sections     Kernel sections that all share the same entrance - the last edge seen
    /// @retval             Section that makes sense based on live parent sections (or nonkernel blocks already executed)
    shared_ptr<CodeSection> findCorrectLiveSection(const set<shared_ptr<CodeSection>, UIDCompare>& sections)
    {
        if( sections.empty() )
        {
            return nullptr;
        }
        else if( sections.size() == 1 )
        {
            return *sections.begin();
        }
        else
        {
            // BW 8/4/22 we have changed the memory pass to only regard basic block sets, thus we don't care about finding exactly the correct kernel that was just entered, we can do that after the profile is done
            return *sections.begin();
        }
    }

    /// @brief Determines if a new iteration of a kernel has begun
    ///
    /// To determine if a new iteration has commenced, we use the entrance edges of a kernel
    /// First, if the incoming edge is an entrance to the live section, a new iteration (the very first) is beginning, so this function returns true
    /// Second, if the incoming edge has as its sink the sink node of an entrance to the live section, but its source node does not match that entrance edge, a new iteration is commencing
    /// @param liveSection  The child-most code section to be live at the moment. 
    /// @param lastEdge     The edge that has just occurred
    /// @retval             Returns true if lastEdge is an iteration edge of liveSection, false otherwise
    bool onNewIteration(const shared_ptr<CodeSection>& liveSection, const pair<int64_t, int64_t>& lastEdge)
    {
        // reverse-maps entrance/exit edges to live sections
        static map<shared_ptr<CodeSection>, set<pair<int64_t, int64_t>>> sectionToEntrance;
        // maps a sink node to a set of source nodes it is allowed to have in order to be an iteration edge
        static map<pair<int64_t, int64_t>, bool> memo;

        // determine if we already have this edge in the memo
        if( memo.find(lastEdge) == memo.end() )
        {
            // check to see if this section has an entry in the sectionToEntrance map and add if necessary
            if( sectionToEntrance.find(liveSection) == sectionToEntrance.end() )
            {
                set<pair<int64_t, int64_t>> entrances;
                for( const auto& entry : enteringEdges )
                {
                    if( entry.second.find(liveSection) != entry.second.end() )
                    {
                        entrances.insert(entry.first);
                    }
                }
                sectionToEntrance[liveSection] = entrances;
            }
            // now use sectionToEntrance to remember this edge
            for( const auto& entry : sectionToEntrance.at(liveSection) )
            {
                if( entry.second == lastEdge.second )
                {
                    // this only leaves two conditions
                    // the last edge landed on the entrance node and did not come from the entrance source, so this is a new iteration
                    // this was an entrance edge, which is the beginning of an iteration
                    memo[lastEdge] = true;
                }
                else
                {
                    // this edge does not land on the correct block in the current kernel instance
                    memo[lastEdge] = false;
                }
            }
        }
        return memo.at(lastEdge);
    }

    void newTime( vector<vector<vector<pair<uint64_t, uint64_t>>>>& DAGArray, vector<vector<vector<pair<uint64_t, uint64_t>>>>::iterator& timeEntry, vector<vector<pair<uint64_t, uint64_t>>>::iterator& depthEntry, vector<pair<uint64_t, uint64_t>>::iterator& breadthEntry )
    {
        // we need new slots in the DAGArray for the upcoming instance
        DAGArray.push_back(vector<vector<pair<uint64_t, uint64_t>>>());
        timeEntry  = prev(DAGArray.end());
        timeEntry->push_back(vector<pair<uint64_t, uint64_t>>());
        depthEntry = prev(timeEntry->end());
        depthEntry->push_back(pair<uint64_t, uint64_t>());
        breadthEntry = prev(depthEntry->end());
    }

    void newDepth( vector<vector<vector<pair<uint64_t, uint64_t>>>>::iterator& timeEntry, vector<vector<pair<uint64_t, uint64_t>>>::iterator& depthEntry, vector<pair<uint64_t, uint64_t>>::iterator& breadthEntry )
    {
        timeEntry->push_back(vector<pair<uint64_t, uint64_t>>());
        depthEntry = prev(timeEntry->end());
        depthEntry->push_back(pair<uint64_t, uint64_t>());
        breadthEntry = prev(depthEntry->end());
    }

    void newBreadth( vector<vector<pair<uint64_t, uint64_t>>>::iterator& depthEntry, vector<pair<uint64_t, uint64_t>>::iterator& breadthEntry )
    {
        depthEntry->push_back(pair<uint64_t, uint64_t>());
        breadthEntry = prev(depthEntry->end());
    }

    /// @brief Pushes a new kernel instance to the DAGArray, making decisions about existing entries in it
    ///
    /// Redundant entries in the tree can occur when multiple kernels are on the same hierarchy level
    /// For example, if we just went from one kernel to another in the same hierarchy level, the depth doesn't change, but we may need a new breadth entry
    /// If the existing breadth entry matches the new kernel instance here, add to it
    void pushToBreadth(const shared_ptr<KernelInstance>& ki, const set<shared_ptr<KernelInstance>, UIDCompare>& kInstances, vector<vector<pair<uint64_t, uint64_t>>>::iterator& depth, vector<pair<uint64_t, uint64_t>>::iterator& breadth)
    {
        // if the existing breadth entry matches the new kernel instance here, add to it
        bool found = false;
        for( auto entry = depth->begin(); entry != depth->end(); entry++ )
        {
            auto existingEntry = kInstances.find(entry->first);
            if( existingEntry != kInstances.end() )
            {
                if( (*existingEntry)->k->IID == ki->k->IID )
                {
                    entry->second++;
                    found = true;
                    break;
                }
            }
#ifdef DEBUG
            else
            {
                spdlog::critical("Could not find an existing instance for an entry in the DAG array!");
                exit(EXIT_FAILURE);
            }
#endif
        }
        if( !found )
        {
            // we need a new entry for this unique kernel child
            newBreadth(depth, breadth);
            breadth->first  = ki->IID;
            breadth->second = 1;
        }
    }

    void BuildDAGArray()
    {
        // the timeline contains all instances of all kernels sequentially as they executed
        // to construct the human-interpretable version of this DAG, we need to condense all like instances together, and weight the edges with the correct number (of iterations)
        set<shared_ptr<CodeInstance>, UIDCompare> instances;
        set<shared_ptr<KernelInstance>, UIDCompare> kInstances;
        set<shared_ptr<NonKernelInstance>, UIDCompare> nkInstances;
        for( const auto& k : blockSets )
        {
            for( const auto& ins : k->getInstances() )
            {
                kInstances.insert(static_pointer_cast<KernelInstance>(ins));
                instances.insert(ins);
            }
        }
        for( const auto& nk : nonKernels )
        {
            for( const auto& ins : nk->getInstances() )
            {
                nkInstances.insert(static_pointer_cast<NonKernelInstance>(ins));
                instances.insert(ins);
            }
        }
        if( instances.empty() )
        {
            return;
        }
        // currentInstance keeps track of the instance we are on
        auto instanceIt = instances.begin();
        // prevIt tells us who the last instance was, to know what the last edge was that was traversed in the codeSection hierarchy
        auto prevIt = instanceIt;
        // time keeps track of the current outermost entry in the DAGArray
        DAGArray.push_back(vector<vector<pair<uint64_t, uint64_t>>>());
        auto time  = DAGArray.begin();
        // depth tracks the current depth entry in the DAGArray
        time->push_back(vector<pair<uint64_t, uint64_t>>());
        auto depth = time->begin();
        // breadth tracks the current breadth entry in the instance map
        depth->push_back(pair<uint64_t, uint64_t>());
        auto breadth = depth->begin();

        while( instanceIt != instances.end() )
        {
            // state machine that updates the tree array with the source node of the hierarchy edge
            auto currentInstance = *instanceIt;
            auto prevInstance    = *prevIt;
            if( auto nki = dynamic_pointer_cast<NonKernelInstance>(currentInstance) )
            {
                if( auto pnki = dynamic_pointer_cast<NonKernelInstance>(prevInstance) )
                {
                    // back to back nonkernel instances is possible in at least one place
                    // 1.) at the start of the program
                    // 2.) there is a non kernel instance that occurs in the program at an early time, then later is found again, this time compounded with another nonkernel section. In this case they will happen back to back

                    // if this is the first case, the DAGArray should only have one time slot in it so far
                    // if this is the second case, the non kernel instances should be 
                    // for both cases, we just add the instance to the array
                    if( DAGArray.size() == 1 )
                    {
                        breadth->first = nki->IID;
                        breadth->second = 1;
                    }
                    else
                    {
                        spdlog::warn("Found multiple nonkernel instances that did not occur at the start of the program!");
                        //exit(EXIT_FAILURE);
                    }
                }
                else
                {
                    auto pki = static_pointer_cast<KernelInstance>(prevInstance);
                    // we have gone from kernel to nonkernel ie a kernel has exited
                    // so we add the last kernel instance to the current location in the array and call the time slot done
                    // first case, the nonkernel is not part of a hierarchy
                    newTime(DAGArray, time, depth, breadth);
                    breadth->first = nki->IID;
                    breadth->second++;
                }
            }
            else if( auto ki = dynamic_pointer_cast<KernelInstance>(currentInstance) )
            {
                if( auto pki = dynamic_pointer_cast<KernelInstance>(prevInstance) )
                {
#if NONKERNEL == 0
                    // every transition will be a transition between kernel and kernel
                    // corner case: when NONKERNEL == 0, this can represent the first entry in the DAG array, and if it is, we just put the current kernel into it
                    if( time == DAGArray.begin() && ki == pki )
                    {
                        // just put currentInstance in and move on
                        breadth->first = ki->IID;
                        breadth->second++;
                    }
                    else
                    {
                        newTime(DAGArray, time, depth, breadth);
                        breadth->first = ki->IID;
                        breadth->second++;                        
                    }
                    prevIt = instanceIt;
                    instanceIt = next(instanceIt);
                    // when nonkernels are off, we don't pay attention to any other type of transition, so we continue
                    continue;
#endif
                    // compare kernels and see if we crossed a codesection hierarchy edge
                    if( ki->k->IID != pki->k->IID )
                    {
#if HIERARCHIES == 1
                        // there are two known cases here
                        // first case, we just crossed a hierarchical boundary
                        // second case, we just went from one child kernel to another, back-to-back

                        // we may have crossed a hierarchical edge from child to parent
                        if( ki->k->children.find(pki->k) != ki->k->children.end() )
                        {
                            // we just went from child to parent kernel
                            // so we have to go to the depth level above this one and add an instance
                            depth = prev(depth);
                            pushToBreadth(ki, kInstances, depth, breadth);
                        }
                        // we may have crossed a hierarchical edge from parent to child
                        else if( ki->k->parents.find(pki->k) != ki->k->parents.end() )
                        {
                            // we just went from parent to child kernel
                            // first figure out if we have a depth level for this child
                            if( depth == prev(time->end()) )
                            {
                                // we are at the bottom of the depth so far, so we need a new depth level
                                newDepth(time, depth, breadth);
                                breadth->first  = ki->IID;
                                breadth->second = 1;
                            }
                            else
                            {
                                // else there is a level below us, we just need a new breadth entry
                                depth = next(depth);
                                breadth = prev(depth->end());
                                pushToBreadth(ki, kInstances, depth, breadth);
                            }
                        }
                        // we may have just gone from one child kernel to another, then both kernels should have exactly the same parent kernels
                        else if( pki->k->parents == ki->k->parents )
                        {
                            // we don't have to change depth level
                            pushToBreadth(ki, kInstances, depth, breadth);
                        }
                        else
                        {
                            // it is possible for kernel hierarchies to span multiple kernel levels (ex. Shared function example, kernels 3 (childmost) 6 (middle) and 8 (parentmost))
                            // for example, in a hierarchy of 3, the child-most can exit, then the middle can exit immediately after.. the instance set won't have an instance for the exiting middle
                            // so what we have to do here is traverse the hierarchy in all possible directions (up or down) until we find the next instance
                            auto prevLevel = pki->k->contextLevel;
                            auto currentLevel    = ki->k->contextLevel;
                            // levelChange can be a positive (up the hierarchy ie child to parent) or negative (down the hierarchy ie parent to child)
                            auto levelChange  = prevLevel - currentLevel;
                            if( levelChange > 0 )
                            {
                                // child to parent
                                while( levelChange > 0 )
                                {
                                    depth = prev(depth);
                                    levelChange--;
                                }
                            }
                            else if( levelChange < 0 )
                            {
                                // parent to child
                                while( levelChange < 0 )
                                {
                                    depth = next(depth);
                                    levelChange++;
                                }
                            }
                            else if( levelChange == 0 )
                            {
                                // shouldn't happen
                                spdlog::critical("Could not determine depth of kernel hierarchy jump!");
                                exit(EXIT_FAILURE);
                            }
                            breadth = prev(depth->end());
                            pushToBreadth(ki, kInstances, depth, breadth);
                        }
#endif
                    }
                    else
                    {
                        // we just went from one instance of a kernel to another
                        // update the current entry instance count
                        breadth->second++;
                    }
                }
                else
                {
                    // we just went from nonkernel to kernel
                    // this can be done in two ways
                    // first, the time slot changed. In this case, we have completed the previous time slot and now require a new one
                    // second, a low-frequency nonkernel loop has called a kernel
                    auto pnki = static_pointer_cast<NonKernelInstance>(prevInstance);
                    // we need a new time slot for the kernel instance
                    newTime(DAGArray, time, depth, breadth);
                    // now populate the new time instance
                    breadth->first = ki->IID;
                    breadth->second = 1;
                }
            }
            prevIt = instanceIt;
            instanceIt = next(instanceIt);
        }
    }

    string GenerateInstanceDot()
    {
        set<shared_ptr<CodeInstance>, UIDCompare> instances;
        set<shared_ptr<KernelInstance>, UIDCompare> kInstances;
        set<shared_ptr<NonKernelInstance>, UIDCompare> nkInstances;
        for( const auto& k : blockSets )
        {
            for( const auto& ins : k->getInstances() )
            {
                kInstances.insert(static_pointer_cast<KernelInstance>(ins));
                instances.insert(ins);
            }
        }
        for( const auto& nk : nonKernels )
        {
            for( const auto& ins : nk->getInstances() )
            {
                nkInstances.insert(static_pointer_cast<NonKernelInstance>(ins));
                instances.insert(ins);
            }
        }
        string dotString = "digraph {\n";
        // label each node after its kernel
        for( const auto& time : DAGArray )
        {
            for( const auto& depth : time )
            {
                for( const auto& breadth : depth )
                {
                    auto instance = *instances.find(breadth.first);
                    if( auto ki = dynamic_pointer_cast<KernelInstance>(instance) )
                    {
                        string kLabel;
                        if( !ki->k->label.empty() )
                        {
                            kLabel = ki->k->label;
                        }
                        else
                        {
                            kLabel = to_string(ki->k->kid);
                        }
                        dotString += "\t"+to_string(ki->IID)+" [label="+kLabel+"];\n";
                    }
                    else if( auto nki = dynamic_pointer_cast<NonKernelInstance>(instance) )
                    {
                        dotString += "\t"+to_string(nki->IID)+" [label="+to_string(nki->nk->IID)+"];\n";
                    }
                }
            }
        }
        // go through the generated array of kernel instances and turn it into a DAG
        for( auto time = DAGArray.begin(); time < prev(DAGArray.end()); time++ )
        {
            // make a solid edge between the current time point and the next
            auto currentBreadth = time->begin()->begin();
            auto nextBreadth = next(time)->begin()->begin();
            dotString += "\t"+to_string(currentBreadth->first)+" -> "+to_string(nextBreadth->first)+" [style=solid];\n";
            for( auto depth = time->begin(); depth < prev(time->end()); depth++ )
            {
                for( auto breadth = depth->begin(); breadth < depth->end(); breadth++ )
                {
                    // build out the depth level below the current
                    auto nextDepth = next(depth);
                    for( auto nextBreadth = nextDepth->begin(); nextBreadth < nextDepth->end(); nextBreadth++ )
                    {
                        // make sure that the parent-child relationship between these two code sections exists
                        if( kInstances.find(breadth->first) != kInstances.end() )
                        {
                            if( kInstances.find(nextBreadth->first) != kInstances.end() )
                            {
                                auto parent = (*kInstances.find(breadth->first))->k;
                                auto child = (*kInstances.find(nextBreadth->first))->k;
                                if( parent->children.find(child) != parent->children.end() )
                                {
                                    dotString += "\t"+to_string(nextBreadth->first)+" -> "+to_string(breadth->first)+" [style=dashed,label="+to_string(nextBreadth->second)+"];\n";
                                }
                            }
                        }
                    }
                }
            }
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

    /// @brief Returns a set of MemTuples in "consumer" whose producers cannot be explained by "producer"
    ///
    /// @param producer     Set of write tuples
    /// @param consumer     Set of tuples (can be read or write) whose tuples may be explained (as in, the last writer to these addresses) by the mem tuples in "producer"
    /// @retval             Pair of values: first is a set of tuples from "consumer" which cannot be explained by the tuples in "producer", second is a bool indicating "true" if the returned set is different than the "consumer" argument
    pair<set<MemTuple, MTCompare>, bool> removeExplainedProducers(const set<MemTuple, MTCompare>& producer, const set<MemTuple, MTCompare>& consumer)
    {
        // this set contains all consumers whose producers cannot be explained with the tuples in producer
        set<MemTuple, MTCompare> unExplainedConsumers = consumer;
        bool changes = false;
        for( const auto& produced : producer )
        {
            for( const auto& consumed : consumer )
            {
                auto overlap = mem_tuple_overlap(produced, consumed);
                if( overlap.base + overlap.offset > 0 )
                {
                    changes = true;
                    remove_tuple_set(unExplainedConsumers, overlap);
                }
            }
        }
        return pair(unExplainedConsumers, changes);
    }

    void GenerateMemoryRegions()
    {
        string csvString = "Hierarchy,Type,Start,End\n";
        for( const auto& regions : hierarchyFootprints )
        {
            for( const auto& rsection : regions.second.first )
            {
                csvString += to_string(regions.first)+",READ,"+to_string(rsection.base)+","+to_string(rsection.base+(uint64_t)rsection.offset)+"\n";
            }
            for( const auto& wsection : regions.second.second )
            {
                csvString += to_string(regions.first)+",WRITE,"+to_string(wsection.base)+","+to_string(wsection.base+(uint64_t)wsection.offset)+"\n";
            }
        }
        ofstream csvFile;
        if( getenv("CSV_FILE") )
        {
            csvFile = ofstream(getenv("CSV_FILE"));
        }
        else
        {
            csvFile = ofstream("MemoryFootprints_Hierarchies.csv");
        }
        csvFile << csvString;
        csvFile.close();
    }

    void GenerateMemoryFootprints()
    {
        // arrays for each instance that was observed in the profile
        set<shared_ptr<CodeInstance>, UIDCompare> instances;
        set<shared_ptr<KernelInstance>, UIDCompare> kInstances;
        set<shared_ptr<NonKernelInstance>, UIDCompare> nkInstances;
        // mapping from a kernel instance to the kernel its memory data should be attributed to
        // - if hierarchies is off, all children will be attributed to the parent-most kernel
        map<shared_ptr<KernelInstance>, shared_ptr<KernelInstance>> attributionMap;
        for( const auto& k : blockSets )
        {
            for( const auto& ins : k->getInstances() )
            {
                auto k = static_pointer_cast<KernelInstance>(ins);
                kInstances.insert(k);
                instances.insert(ins);
#if HIERARCHIES == 0
                if( k->parent )
                {
                    // find the parentmost kernel and map this instance to that
                    auto target = k->parent;
                    while( true )
                    {
                        if( target->parent )
                        {
                            target = target->parent;
                        }
                        else
                        {
                            break;
                        }
                    }
                    attributionMap[k] = target;
                }
                else
                {
                    attributionMap[k] = k;
                }
#endif
            }
        }
        for( const auto& nk : nonKernels )
        {
            for( const auto& ins : nk->getInstances() )
            {
                nkInstances.insert(static_pointer_cast<NonKernelInstance>(ins));
                instances.insert(ins);
            }
        }
        // use DAGArray to find the memory footprints of each hierarchy in the graph
        for( unsigned i = 0; i < DAGArray.size(); i++ )
        {
            auto readTuple = set<MemTuple, MTCompare>();
            auto writeTuple = set<MemTuple, MTCompare>();
            for( const auto& depth : DAGArray[i] )
            {
                for( const auto& breadth : depth )
                {
                    shared_ptr<CodeInstance> instance;
                    if( kInstances.find(breadth.first) != kInstances.end() )
                    {
                        instance = *kInstances.find(breadth.first);
                    }
                    else
                    {
                        instance = *nkInstances.find(breadth.first);
                    }
                    auto instanceRTuple = set<MemTuple, MTCompare>();
                    auto instanceWTuple = set<MemTuple, MTCompare>();
                    for( const auto& rtuple : instance->getMemory().rTuples )
                    {
                        if( rtuple.offset > MIN_TUPLE_OFFSET )
                        {
                            merge_tuple_set(instanceRTuple, rtuple);
                        }
                    }
                    for( const auto& wtuple : instance->getMemory().wTuples )
                    {
                        if( wtuple.offset > MIN_TUPLE_OFFSET )
                        {
                            merge_tuple_set(instanceWTuple, wtuple);
                        }
                    }
                    // use the map to attribute this memory to the correct kernel
                    if( auto ki = dynamic_pointer_cast<KernelInstance>(instance) )
                    {
                        CIFootprints[ attributionMap.at(ki)->IID ].first  = instanceRTuple;
                        CIFootprints[ attributionMap.at(ki)->IID ].second = instanceWTuple;
                    }
                    else
                    {
                        CIFootprints[instance->IID].first  = instanceRTuple;
                        CIFootprints[instance->IID].second = instanceWTuple;
                    }
                    for( const auto& r : instanceRTuple )
                    {
                        merge_tuple_set(readTuple, r);
                    }
                    for( const auto& w : instanceWTuple )
                    {
                        merge_tuple_set(writeTuple, w);
                    }
                }
            }
            hierarchyFootprints[i].first  = readTuple;
            hierarchyFootprints[i].second = writeTuple;
        }
        // outputs a csv of the memory footprints for each time slot
        GenerateMemoryRegions();
    }

    map<uint32_t, set<uint32_t>> GenerateTimeSlotDependencies()
    {
        // This data structure maps each time period to its producer dependencies 
        map<uint32_t, set<uint32_t>> consumerDepMap;
        // walk backwards through hierarchyFootprints to find out who the last writers are for a given read set
        for( unsigned i = (unsigned int)(hierarchyFootprints.size()-1); i > 0; i-- )
        {
            // for each step before this one, look for writers of the data the current step reads
            // each time there is overlap, we need to correct the reader tuple (because those addresses have been explained)
            auto currentTupleSet = hierarchyFootprints.at(i).first;
            int j = (int)i-1;
            while( !currentTupleSet.empty() && (j >= 0) )
            {
                auto newTupleSet = removeExplainedProducers(hierarchyFootprints.at((unsigned)j).second, currentTupleSet);
                if( newTupleSet.second )
                {
                    consumerDepMap[i].insert((unsigned)j);
                    currentTupleSet = newTupleSet.first;
                }
                j--;
            }
        }
        return consumerDepMap;
    }

    map<uint64_t, pair<set<uint64_t>, set<uint64_t>>> GenerateTaskCommunication()
    {
        // maps a code instance ID to its RAW (first) and WAW (second) dependencies
        map<uint64_t, pair<set<uint64_t>, set<uint64_t>>> taskCommunication;
        // first check to see if CIFootprints is even the correct size
        if( CIFootprints.size() < 2 )
        {
            spdlog::warn("No memory dependency information can be generated because there is only one code instance");
            return taskCommunication;
        }
        // walk backwards through the code instance footprints to generate RAW and WAW dependencies
        for( auto ti = CIFootprints.rbegin(); ti != prev(CIFootprints.rend()); ti++ )
        {
            // first do RAW dependencies
            auto consumed = ti->second.first;
            auto producer = next(ti);
            while( !consumed.empty() && (producer != CIFootprints.rend()) )
            {
                auto newTupleSet = removeExplainedProducers(producer->second.second, consumed);
                if( newTupleSet.second )
                {
                    taskCommunication[ti->first].first.insert(producer->first);
                    consumed = newTupleSet.first;
                }
                producer = next(producer);
            }
            // second do WAW dependencies
            auto producedLater = ti->second.second;
            producer = next(ti);
            while( !producedLater.empty() && (producer != CIFootprints.rend()) )
            {
                auto newTupleSet = removeExplainedProducers(producer->second.second, producedLater);
                if( newTupleSet.second )
                {
                    taskCommunication[ti->first].second.insert(producer->first);
                    producedLater = newTupleSet.first;
                }
                producer = next(producer);
            }
        }
        return taskCommunication;
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

    void iterateNonKernel(uint64_t a, const pair<int64_t, int64_t>& crossedEdge)
    {
        // this check ensures that nonKernelBlocks contains the block, otherwise the profiler doesn't have a live kernel when it should
#ifdef DEBUG
        if( (nonKernelBlocks.find((int64_t)a) == nonKernelBlocks.end()) && (kernelBlocks.find((int64_t)a) != kernelBlocks.end() ) )
        {
            spdlog::critical("No kernels are live, but the current block belongs to at least one kernel and doesn't belong to any nonKernels!");
            exit(EXIT_FAILURE);
        }
        else if( (nonKernelBlocks.find((int64_t)a) == nonKernelBlocks.end()) && (kernelBlocks.find((int64_t)a) == kernelBlocks.end()) )
        {
            spdlog::critical("Block was not accounted for in kernelBlocks, nonKernelBlocks set!");
            exit(EXIT_FAILURE);
        }
        // else if only in nonkernel set, okay
        // else if in both sets, shared function block, also okay
#endif
        // we have entered a non-kernel
        // it is possible that we have either entered a new (nonkernel) section of the code not yet touched, or it is also possible that we have entered a non-kernel that already exists (because nonkernels can iterate a low number of times)
        // see if the current block has already been touched
        if( executedBlocks.find((int64_t)a) == executedBlocks.end() )
        {
            // here we may be in a new kernel, if there isn't one yet
            if( !currentNKI )
            {
                // we should make a new non-kernel
                auto newNK = make_shared<NonKernel>(crossedEdge);
                currentNKI = make_shared<NonKernelInstance>(newNK);
                newNK->addInstance(currentNKI);
                enteringEdges[crossedEdge].insert(newNK);
                nonKernels.insert(newNK);
                ++InstanceCount[newNK];
                TimeLine.push_back( pair(newNK->IID, InstanceCount.at(newNK)) );
            }
            // else we just add to the existing
            else
            {
                currentNKI->nk->blocks.insert((int64_t)a);
            }
        }
        else
        {
            // this block has already been touched, meaning there are two possible cases
            // 1. This block belongs to a shared function that both belongs to a kernel and non-kernel. In this case the block may already exist within a nonkernel, so search for it. If there are none found with this block, make a new one
            // 2. This block doesn't belong to a shared function. In this case the block must belong to an existing non-kernel, so find it
            // in both cases we have to look for the nonkernel that has this block
            shared_ptr<NonKernel> nk = nullptr;
            for( const auto& n : nonKernels )
            {
                if( n->blocks.find((int64_t)a) != n->blocks.end() )
                {
                    nk = n;
                    break;
                }
            }
            // If we have case 1, the block should be in the kernelBlocks set
            if( kernelBlocks.find((int64_t)a) != kernelBlocks.end() )
            {
                // confirmed we have case 1. If we don't find a nonkernel with this block, make a new one
                if( nk == nullptr )
                {
                    auto newNK = make_shared<NonKernel>(crossedEdge);
                    currentNKI = make_shared<NonKernelInstance>(newNK);
                    newNK->addInstance(currentNKI);
                    enteringEdges[crossedEdge].insert(newNK);
                    nonKernels.insert(newNK);
                    TimeLine.push_back( pair(newNK->IID, 1) );
                }
                // else make a new instance for the existing one
                else
                {
                    currentNKI = make_shared<NonKernelInstance>(nk);
                    nk->addInstance(currentNKI);
                    nk->blocks.insert((int64_t)a);
                    ++InstanceCount[currentNKI->nk];
                    TimeLine.push_back( pair(currentNKI->nk->IID, InstanceCount.at(currentNKI->nk)) );
                }
            }
            else
            {
                // we know this block is not a kernel block and it has been executed already
                if( nk == nullptr )
                {
                    // it doesn't yet belong to a nonkernel, which this is a problem
                    spdlog::critical("Found a nonkernel-only block that has already executed and has not been structured into a nonkernel!");
                    exit(EXIT_FAILURE);
                }
                else
                {
                    // if we don't have a nonkernel instance yet, make a new one
                    if( !currentNKI )
                    {
                        currentNKI = make_shared<NonKernelInstance>(nk);
                        nk->addInstance(currentNKI);
                        ++InstanceCount[currentNKI->nk];
                        TimeLine.push_back( pair(currentNKI->nk->IID, InstanceCount.at(currentNKI->nk)) );
                    }
                    // else it is technically an iteration, but we don't keep track of this
                    nk->blocks.insert((int64_t)a);
                }
            }
        }
    }

    extern "C"
    {
        void __TraceAtlas__Profile__Backend__MemoryDestroy()
        {
            memoryActive = false;
            // this is an implicit exit, so store the current iteration information to where it belongs
            if( currentKI )
            {
                currentKI->addIteration(currentIteration);
            }
            if( currentNKI )
            {
                currentNKI->addIteration(currentIteration);
            }
            BuildDAGArray();
            GenerateMemoryFootprints();
            GenerateTaskGraph();
            GenerateTimeSlotDependencies();
        }

        void __TraceAtlas__Profile__Backend__MemoryIncrement(uint64_t a)
        {
            // if the profile is not active, we return
            if (!memoryActive)
            {
                return;
            }
            auto crossedEdge = pair(lastBlock, (int64_t)a);
            // exiting sections
            if (exitingEdges.find(crossedEdge) != exitingEdges.end())
            {
                for (auto ex : exitingEdges.at(crossedEdge) )
                {
                    if( auto k = dynamic_pointer_cast<Kernel>(ex) )
                    {
                        liveKernels.erase(k);
                        // remember iteration information
                        currentKI->addIteration(currentIteration);
                        // set the current iteration to the one that we saved before going into the exiting kernel instance.. if necessary
                        if( !liveKernels.empty() )
                        {
                            auto ci = *liveKernels.rbegin();
                            if( auto ki = dynamic_pointer_cast<Kernel>(ci) )
                            {
                                currentKI = ki->getCurrentKI();
                            }
                            else 
                            {
                                spdlog::critical("Found a non-kernel entity inside the liveKernels set!");
                                exit(EXIT_FAILURE);
                            }
                        }
                    }
#if NONKERNEL
                    else if( auto nk = dynamic_pointer_cast<NonKernel>(ex) )
                    {
                        // this is the end of a nonkernel instance... remember the iteration information
                        // if that non kernel instance is alive push the currentIteration to it
                        if( currentNKI )
                        {
                            if( currentNKI->nk == nk )
                            {
                                currentNKI->addIteration(currentIteration);
                            }
                        }
                    }
#endif
                }
            }
            // entering sections
            if (enteringEdges.find(crossedEdge) != enteringEdges.end())
            {
                // cases
                // 1. we have a new instance of somebody 
                //  -> two cases
                //      -- we have exited another code section and entered the current
                //      -- just started the program and need to make the first nonkernel instance
                // 2. we have just exited an iteration of a child kernel and entered a parent kernel
                auto newSection = findCorrectLiveSection(enteringEdges.at(crossedEdge));
                if( auto newKernel = dynamic_pointer_cast<Kernel>(newSection) )
                {
                    liveKernels.insert(newKernel);
                    ++InstanceCount[newKernel];
                    TimeLine.push_back(pair(newKernel->IID, InstanceCount[newKernel]));
                    // this entrance edge denotes a new kernel iteration
                    // we remember currentIteration by pushing it to the parent's list, if there is a parent
                    if( currentKI )
                    {
                        // there is an active kernel whose iteration data needs to be saved
                        // later, when we get back to currentKI, its currentIteration will be put into currentIteration again
                        currentKI->addIteration(currentIteration);
                    }
                    currentKI = make_shared<KernelInstance>(static_pointer_cast<Kernel>(newKernel));
                    newKernel->addInstance(currentKI);
#if NONKERNEL
                    // take care of any non-kernels we may have just exited
                    if( currentNKI )
                    {
                        // the nonkernel is allowed to iterate a low number of times
                        // naively, we cannot determine if the nonkernel code can reach this point again
                        // so, the best we can do is to close this nonkernel, and if we reach this point again, we can append code that loops back to here to the existing non-kernel                    
                        currentNKI->nk->exits[crossedEdge.first].insert(crossedEdge.second);
                        exitingEdges[crossedEdge].insert( currentNKI->nk );
                        currentNKI->addIteration(currentIteration);
                        currentNKI = nullptr;
                    }
#endif
                    // lastly, after pushing the iteration information to the former kernel instance, we clear currentIteration
                    currentIteration.clear();
                }
                else
                {
#if NONKERNEL
                    // we have entered a non-kernel
                    // it is possible for kernels to be live when a nonkernel is entered (that is, when a nonkernel is within a parent kernel)
                    // it is also possible for a nonkernel to be live when a nonkernel is entered (if this is the second half of a low-frequency loop and we have just cycles to the first half again)
                    // in the case that we have just entered a nonkernel when a nonkernel is live, we can merge them together
                    if( currentNKI )
                    {
                        // these two NKs can be merged
                        // merge into currentNK because it will contain the second half of the loop as well
                        currentNKI->nk->blocks.insert(newSection->blocks.begin(), newSection->blocks.end());
                        for( const auto& ent : newSection->entrances )
                        {
                            for( const auto& dest : ent.second )
                            {
                                currentNKI->nk->entrances[ent.first].insert(dest);
                            }
                            enteringEdges[crossedEdge].erase(newSection);
                            enteringEdges[crossedEdge].insert(currentNKI->nk);
                        }
                        for( const auto& ex : newSection->exits )
                        {
                            for( const auto& dest : ex.second )
                            {
                                currentNKI->nk->exits[ex.first].insert(dest);
                            }
                            exitingEdges[crossedEdge].erase(newSection);
                            exitingEdges[crossedEdge].insert(currentNKI->nk);
                        }
                        nonKernels.erase(static_pointer_cast<NonKernel>(newSection));
                    }
                    else
                    {
                        // we have entered a non kernel, add an instance to it
                        currentNKI = make_shared<NonKernelInstance>(static_pointer_cast<NonKernel>(newSection));
                        newSection->addInstance(currentNKI);
                    }
#endif
                }
            }
            // when no kernels are live, this is an indication that a non-kernel is being descovered
            if( liveKernels.empty() )
            {
#if NONKERNEL
                iterateNonKernel(a, crossedEdge);
#endif
            }
            else
            {
                // check for a new iteration of the current section
                if( onNewIteration(*liveKernels.rbegin(), crossedEdge) )
                {
                    if( currentKI )
                    {
                        auto cpy = currentKI;
                        currentKI->addIteration(currentIteration);
                    }
                    currentIteration.clear();
                }
            }
#if NONKERNEL
            executedBlocks.insert((int64_t)a);
#endif
            lastBlock = (int64_t)a;
        }

        void __TraceAtlas__Profile__Backend__MemoryStore(void *address, uint64_t bbID, uint32_t instructionID, uint64_t datasize)
        {
            static MemTuple mt;
            mt.type = __TA_MemType::Writer;
            if (!memoryActive)
            {
                return;
            }
            mt.base = (uint64_t)address;
            mt.offset = (uint32_t)datasize;
            mt.refCount = 0;
#if NONKERNEL
            merge_tuple_set(currentIteration.wTuples, mt);
#else
            if( currentKI )
            {
                merge_tuple_set(currentIteration.wTuples, mt);
            }
#endif
        }

        void __TraceAtlas__Profile__Backend__MemoryLoad(void *address, uint64_t bbID, uint32_t instructionID, uint64_t datasize)
        {
            static MemTuple mt;
            mt.type = __TA_MemType::Reader;
            if (!memoryActive)
            {
                return;
            }
            mt.base = (uint64_t)address;
            mt.offset = (uint32_t)datasize;
            mt.refCount = 0;
#if NONKERNEL
            merge_tuple_set(currentIteration.rTuples, mt);
#else
            if( currentKI )
            {
                merge_tuple_set(currentIteration.rTuples, mt);
            }
#endif
        }

        void __TraceAtlas__Profile__Backend__MemoryInit(uint64_t a)
        {
            ReadKernelFile();
            try
            {
                FindEpochBoundaries();
            }
            catch (AtlasException &e)
            {
                spdlog::critical(e.what());
                exit(EXIT_FAILURE);
            }
            for (const auto &c : codeSections)
            {
                InstanceCount[c] = 0;
            }
            memoryActive = true;
            lastBlock = (int64_t)a;

            // the first block of main should never belong to a kernel
            if( kernelBlocks.find((int64_t)a) != kernelBlocks.end() )
            {
                spdlog::critical("First block of main belongs to a kernel!");
                exit(EXIT_FAILURE);
            }
#if NONKERNEL
            // make a new nonkernel for the first block
            auto newNK = make_shared<NonKernel>( pair(lastBlock, a) );
            enteringEdges[pair(lastBlock, a)].insert(newNK);
            currentNKI = make_shared<NonKernelInstance>(newNK);
            newNK->addInstance(currentNKI);
            ++InstanceCount[newNK];
            nonKernels.insert(newNK);
            TimeLine.push_back( pair(currentNKI->nk->IID, InstanceCount.at(currentNKI->nk)) );
            executedBlocks.insert((int64_t)a);
#endif
        }
    }
} // namespace TraceAtlas::Backend::BackendMemory