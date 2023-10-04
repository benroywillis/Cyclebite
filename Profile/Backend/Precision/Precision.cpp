//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Util/Exceptions.h"
#include "Util/IO.h"
#include "Precision.h"
#include "Epoch.h"
#include "CodeSection.h"
#include "IO.h"
#include "Processing.h"
#include "Memory.h"

using namespace std;
using json = nlohmann::json;
using namespace llvm;

namespace Cyclebite::Profile::Backend::Precision
{
    /// Timing information
    struct timespec start, end;
    /// Records all values observed during execution
    map<shared_ptr<Cyclebite::Profile::Backend::Memory::Epoch>, ValueHistogram, Cyclebite::Profile::Backend::Memory::UIDCompare> hist;
    /// On/off switch for the profiler
    bool precisionActive = false;
    /// Tracks the last block that was seen
    int64_t lastBlock;

    /// Holds the current kernel instance(s)
    shared_ptr<Cyclebite::Profile::Backend::Memory::Epoch> currentEpoch;
    /// holds all epochs that have been observed
    set<shared_ptr<Cyclebite::Profile::Backend::Memory::Epoch>, Cyclebite::Profile::Backend::Memory::UIDCompare> epochs;
    uint16_t getExponent(uint64_t val, const PrecisionValue& v)
    {
        switch(v.t)
        {
            case PrecisionType::float128:
                throw CyclebiteException("Cannot support a 128-bit float value! The passed value is only 8 bytes.");
            
            case PrecisionType::float80:
                throw CyclebiteException("Cannot support an 80-bit float on this target!");

            case PrecisionType::float64:
                {
                    auto f = *(double*)&val;
                    return (uint16_t)log2( abs(f) );
                }
            case PrecisionType::float32:
                {
                    auto f = *(float*)&val;
                    return (uint16_t)log2( abs(f) );
                }
            case PrecisionType::float16:
                throw CyclebiteException("Cannot support a 16-bit float on this target!");

            case PrecisionType::uint64_t:
                {
                    auto i = *(uint64_t*)&val;
                    return (uint16_t)log2( i );
                }
            case PrecisionType::int64_t:
                {
                    auto i = *(int64_t*)&val;
                    return (uint16_t)log2( abs(i) );
                }
            case PrecisionType::uint32_t:
                {
                    auto i = *(uint32_t*)&val;
                    return (uint16_t)log2(i);
                }
            case PrecisionType::int32_t:
                {
                    auto i = *(int32_t*)&val;
                    return (uint16_t)log2(i);
                }
            case PrecisionType::uint16_t:
                {
                    auto i = *(uint16_t*)&val;
                    return (uint16_t)log2(i);
                }
            case PrecisionType::int16_t:
                {
                    auto i = *(int16_t*)&val;
                    return (uint16_t)log2( abs(i) );
                }
            case PrecisionType::uint8_t:
                {
                    auto i = *(uint8_t*)&val;
                    return (uint16_t)log2(i);
                }
            case PrecisionType::int8_t:
                {
                    auto i = *(int8_t*)&val;
                    return (uint16_t)log2( abs(i) );
                }
            case PrecisionType::uint1_t:
                {
                    return 0;
                }
            case PrecisionType::int1_t:
                {
                    return 0;
                }
            case PrecisionType::vector:
                {
                    // don't care, return 0
                    return 0;
                }
            case PrecisionType::void_t:
                {
                    // don't care, return 0
                    return 0;
                }
            default:
                return 0;
        }
    }

    void PrintTaskHistograms()
    {
        // histograms are printed as a csv
        // histogram bin label is the first row
        // each following row has in each column the magnitude of that category for the corresponding task ID 
        // each row has a task ID in the first column
        
        // make the first row
        string csvString = "TaskID";
        uint32_t maxBin = 0;
        // stencilChain/Naive seems to break this
        // either that or a value's exponent is not being interpreted correctly... which could have to do with signed-ness or some kind of bad cast
        for( const auto& task : hist )
        {
            for( const auto& bin : task.second.exp )
            {
                if( bin.first > maxBin )
                {
                    maxBin = bin.first;
                }
            }
        }
        for( unsigned i = 0; i < maxBin; i++ )
        {
            csvString += ","+to_string(i);
        }
        csvString += "\n";

        // now make a row for each task
        for( const auto& epoch : hist )
        {
            // ID first, if the epoch is a task it gets the task ID, else it gets the epoch ID
            /*if( epoch.first->kernel )
            {
                csvString += to_string(epoch.first->kernel->kid);
            }
            else
            {*/
                csvString += to_string(epoch.first->IID);
            //}
            // print each magnitude in the row
            for( unsigned i = 0; i < maxBin; i++ )
            {
                if( epoch.second.find(i) )
                {
                    csvString += ","+to_string(epoch.second[i]);
                }
                else
                {
                    csvString += ",0";
                }
            }
            csvString += "\n";
        }
        string name = "hist.csv";
        if( getenv("HIST_FILE") )
        {
            name = string(getenv("HIST_FILE"));
        }
        ofstream histCsv(name);
        histCsv << csvString;
        histCsv.close();
    }

    extern "C"
    {
        void __Cyclebite__Profile__Backend__PrecisionDestroy()
        {
            while( clock_gettime(CLOCK_MONOTONIC, &end) ) {}
            spdlog::info( "PRECISIONPROFILETIME: "+to_string(CalculateTime(&start, &end))+"s");
            // this is an implicit exit, so store the current iteration information to where it belongs
            epochs.insert(currentEpoch);
            PrintTaskHistograms();
            precisionActive = false;
        }
        void __Cyclebite__Profile__Backend__PrecisionIncrement(uint64_t a)
        {
            // if the profile is not active, we return
            if (!precisionActive)
            {
                return;
            }
            auto crossedEdge = pair(lastBlock, (int64_t)a);
            // exiting sections
            if (Cyclebite::Profile::Backend::Memory::epochBoundaries.find(crossedEdge) != Cyclebite::Profile::Backend::Memory::epochBoundaries.end())
            {
                currentEpoch->exits[lastBlock].insert((int64_t)a);
                epochs.insert(currentEpoch);
                currentEpoch = make_shared<Cyclebite::Profile::Backend::Memory::Epoch>();
                currentEpoch->updateBlocks((int64_t)a);
                currentEpoch->entrances[lastBlock].insert((int64_t)a);
                hist[currentEpoch] = ValueHistogram();
            }
            else
            {
                currentEpoch->updateBlocks((int64_t)a);
            }
#if NONKERNEL
            executedBlocks.insert((int64_t)a);
#endif
            lastBlock = (int64_t)a;
        }
        void __Cyclebite__Profile__Backend__PrecisionStore(uint64_t value, uint64_t bbID, uint32_t instructionID, uint8_t type)
        {
            if( !precisionActive )
            {
                return;
            }
            static PrecisionValue v;
            v.bb  = (uint32_t)bbID;
            v.iid = instructionID;
            v.t   = static_cast<PrecisionType>(type);
            v.op  = PrecisionMemOp::Store;
            v.exp = getExponent(value, v); // some function that can find the exponent of any value
            hist.at(currentEpoch).inc(v.exp);
        }
        void __Cyclebite__Profile__Backend__PrecisionLoad(uint64_t value, uint64_t bbID, uint32_t instructionID, uint8_t type)
        {
            if( !precisionActive )
            {
                return;
            }
            static PrecisionValue v;
            v.bb  = (uint32_t)bbID;
            v.iid = instructionID;
            v.t   = static_cast<PrecisionType>(type);
            v.op  = PrecisionMemOp::Load;
            v.exp = getExponent(value, v); // some function that can find the exponent of any value
            hist.at(currentEpoch).inc(v.exp);
        }
        void __Cyclebite__Profile__Backend__PrecisionInit(uint64_t a)
        {
            Cyclebite::Profile::Backend::Memory::ReadKernelFile();
            try
            {
                Cyclebite::Profile::Backend::Memory::FindEpochBoundaries();
            }
            catch (CyclebiteException &e)
            {
                spdlog::critical(e.what());
                exit(EXIT_FAILURE);
            }
            currentEpoch = make_shared<Cyclebite::Profile::Backend::Memory::Epoch>();
            currentEpoch->updateBlocks((int64_t)a);
            currentEpoch->entrances[(int64_t)a].insert((int64_t)a);
            hist[currentEpoch] = ValueHistogram();

            while( clock_gettime(CLOCK_MONOTONIC, &start) ) {}
            precisionActive = true;
            lastBlock = (int64_t)a;
        }
    }
} // namespace Cyclebite::Backend::Precision