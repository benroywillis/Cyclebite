//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Export.h"
#include "Task.h"
#include "Reduction.h"
#include "InductionVariable.h"
#include "BasePointer.h"
#include "IO.h"
#include "Graph/inc/IO.h"
#include "Collection.h"
#include "Util/Print.h"
#include "Util/Exceptions.h"
#include "TaskParameter.h"
#include "ConstantArray.h"

using namespace std;
using namespace Cyclebite::Grammar;

// instantiation of the map that holds the globals for export
map<const llvm::Constant*, shared_ptr<ConstantSymbol>> Cyclebite::Grammar::constants;

string labelLUT( int noInputs, int noOutputs, vector<int> inputDimensions, vector<int> outputDimensions, bool reduction, int reductionDimensions, bool inPlace, bool parallel )
{
    // LUT
    // Task     | # of inputs |      # of outputs      | # of input dimensions | # of output dimensions | reduction | reduction dimensions | special  |
    // Init     |      0      |          any           |          any          |           any          |     0     |           -          | "rand()" |
    // ZIP      |      2      |           1            |        any,any        |           any          |     0     |           -          |          |
    // Map      |      1      | 1 (works out of place) |          any          |       same as input    |     0     |           -          |          |
    // Foreach  |      1      |  0 (worked in-place)   |          any          |       same as input    |     0     |           -          |          |
    // GEMV     |      2      |           1            |          2,1          |            1           |     1     |           1          |          |
    // GEMM     |      2      |           1            |          2,2          |            2           |     1     |           1          |          |
    // Stencil  |      1      |           1            |           2           |       same as input    |     1     |           2          |          |
#if DEBUG
    string inDimString = "";
    if( !inputDimensions.empty() )
    {
        auto it = inputDimensions.begin();
        inDimString += to_string( *it );
        it = next(it);
        while( it != inputDimensions.end() )
        {
            inDimString += ","+to_string(*it);
            it = next(it);
        }
    }
    string outDimString = "";
    if( !outputDimensions.empty() )
    {
        auto it = outputDimensions.begin();
        outDimString += to_string( *it );
        it = next(it);
        while( it != outputDimensions.end() )
        {
            outDimString += ","+to_string(*it);
            it = next(it);
        }
    }
    spdlog::info("# Inputs: "+to_string(noInputs)+
                 "; # Outputs: "+to_string(noOutputs)+
                 "; inDims: "+inDimString+
                 "; outDims: "+outDimString+
                 "; reduction: "+to_string(reduction)+
                 "; redDims: "+to_string(reductionDimensions)+
                 "; inPlace: "+to_string(inPlace));
#endif
    if( parallel )
    {
        switch( noInputs )
        {
            case 0:
            {
                return reduction ? "Stencil" : "Init";
            }
            case 1:
            {
                if( inPlace )
                {
                    return reduction ? "Stencil" : "Foreach";
                }
                else
                {
                    return reduction ? "Stencil" : "Map";
                }
            }
            case 2: 
            {
                if( reduction )
                {
                    if( std::find(inputDimensions.begin(), inputDimensions.end(), 1) != inputDimensions.end() )
                    {
                        return "GEMV";
                    }
                    else
                    {
                        return "GEMM";
                    }
                }
                else
                {
                    return "ZIP";
                }
            }
            case 4:
            {
                if( reduction && !inPlace && (noOutputs == 1) )
                {
                    return "CGEMM";
                }
                else if( !inPlace && (noOutputs == 1) )
                {
                    return "Map";
                }
                else
                {
                    return "Unknown";
                }
            }
            default: 
            {
                // Map tasks are allowed to have as many inputs as is required
                // since we know this task is parallel, if it doesn't work in-place and produces a single output, it is a map
                if( !inPlace && (noOutputs == 1) )
                {
                    return "Map";
                }
                return "Unknown";
            }
        }
    }
    else
    {
        return "NotParallel";
    }
}

string MapTaskToName( const shared_ptr<Expression>& expr, const set<shared_ptr<Cycle>>& parallelCycles )
{
    // measures
    // 1. number of inputs
    // 2. number of outputs
    // 3. number of input dimensions
    // 4. number of output dimensions
    // 5. reduction
    // 6. reduction dimensions
    
    set<shared_ptr<BasePointer>> inputs;
    for( const auto& in : expr->getInputs() )
    {
        if( const auto& coll = dynamic_pointer_cast<Collection>(in) )
        {
            inputs.insert(coll->getBP());
        }
    }
    // input dimensions are measured by the max number of dimensions in an input
    vector<int> inDims;
    int inMax = INT_MAX;
    for( const auto& in : expr->getInputs() )
    {
        if( const auto& coll = dynamic_pointer_cast<Collection>(in) )
        {
            inDims.push_back((int)coll->getNumDims());
            inMax = (int)coll->getNumDims() < inMax ? (int)coll->getNumDims() : inMax;
        }
        else
        {
            inDims.push_back(0);
        }
    }
    vector<int> outDims;
    if( const auto& coll = dynamic_pointer_cast<Collection>(expr->getOutput()) )
    {
        outDims.push_back((int)coll->getNumDims());
    }
    else
    {
        outDims.push_back(0);
    }
    int redDims = 0;
    for( const auto& rv : expr->getRVs() )
    {
        if( redDims < (int)rv->getDimensions().size() )
        {
            redDims = (int)rv->getDimensions().size();
        }
    }
    bool inPlace = false;
    for( const auto& in : expr->getInputs() )
    {
        if( in == expr->getOutput() )
        {
            inPlace = true;
        }
        else
        {
            inPlace = false;
            break;
        }
    }
    return labelLUT( (int)inputs.size(), expr->getOutput() ? 1 : 0, inDims, outDims, (bool)redDims, redDims, inPlace, !parallelCycles.empty() );    
}

set<shared_ptr<Cycle>> ParallelizeCycles( const shared_ptr<Expression>& expr )
{
    // holds cycles whose execution can be done in parallel
    set<shared_ptr<Cycle>> parallelSpots;
    // holds cycles that cannot be executed fully parallel
    set<shared_ptr<Cycle>> noParallel;
    // holds index variables that are common among the input and output
    set<shared_ptr<IndexVariable>> overlaps;
    shared_ptr<Collection> output = nullptr;
    if( auto array = dynamic_pointer_cast<Collection>(expr->getOutput()) )
    {
        output = array;
    }
    else
    {
        throw CyclebiteException("Cannot yet handle a task whose output is not a collection!");
    }
    // first, find out which dimensions of the inputs to the expression overlap with the output
    // this will tell us which dimensions cannot be parallelized
    for( const auto& input : expr->getInputs() )
    {
        if( const auto& array = dynamic_pointer_cast<Collection>(input) )
        {
            // compare the input space to the output space to find overlap between the two
            // trivial case, if the input is the output, we have overlap
            auto overlap = array->overlaps(output);
            if( !overlap.empty() )
            {
                spdlog::info("Overlap detected between collections "+array->dump()+" and "+output->dump()+":");
                for( const auto& o : overlap )
                {
                    spdlog::info(o->dump());
                    overlaps.insert(o);
                }
            }
        }
    }
    for( const auto& o : overlaps )
    {
        for( const auto& iv : o->getExclusiveDimensions() )
        {
            noParallel.insert(iv->getCycle());
        }
    }
    // second, look for a reduction in the expression
    // this will unlock special optimizations for the algorithm
    shared_ptr<ReductionVariable> rv = nullptr;
    if( expr->hasParallelReduction() )
    {
        for( const auto& rv : expr->getRVs() )
        {
            for( const auto& dim : rv->getDimensions() )
            {
                parallelSpots.insert(dim->getCycle());
            }
        }
    }
    else if( !expr->getRVs().empty() )
    {
        for( const auto& rv : expr->getRVs() )
        {
            for( const auto& dim : rv->getDimensions() )
            {
                noParallel.insert(dim->getCycle());
            }
        }
    }
    // finally, print and return parallel cycles
    for( const auto& c : expr->getTask()->getCycles() )
    {
        if( !noParallel.contains(c) )
        {
            string print = "Cycle "+to_string(c->getID())+" ( blocks: ";
            for( const auto& b : c->getBody() )
            {
                print += to_string( b->originalBlocks.front() )+" ";
            }
            print += ") in Task"+to_string(expr->getTask()->getID())+" is parallel!";
            spdlog::info(print);
            parallelSpots.insert(c);
        }
    }
    return parallelSpots;
}

/// @brief Vectorizes reductions
///
/// The inner-most loop of the reduction will be vectorized with "#pragma omp simd"
/// We assume that all reductions, regardless of their underlying data type, to be fully associated.
/// This will result in an error as described in https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html
/// @param red  The reduction to vectorize. Only its inner-most loop will be vectorized with "pragma omp simd"
set<shared_ptr<Cycle>> VectorizeExpression( const shared_ptr<Expression>& expr )
{
    set<shared_ptr<Cycle>> reductionCycles;
    if( expr->hasParallelReduction() )
    {
        for( const auto& rv : expr->getRVs() )
        {
            for( const auto& dim : rv->getDimensions() )
            {
                reductionCycles.insert(dim->getCycle());
            }
        }
    }
    return reductionCycles;
}

void exportHalide( const map<shared_ptr<Task>, vector<shared_ptr<Expression>>>& taskToExpr, const map<shared_ptr<Task>, set<string>>& taskLabels )
{
    string pipelineName = "CyclebiteGEMM";
    // Things to dwell on
    // 1. Non-task code
    //    - non-task code doesn't split tasks
    //      -- trivially delete
    //    - non-task code that splits tasks 
    //      -- non-task code that doesn't produce anything
    //         --- probably just implementation-specific control-flow, delete from pipeline
    //      -- non-task code that produces something
    //         --- needs to be scheduled before its consumer (pre-task header?)
    // 2. Multiple task instances
    //    - Task instances are seperate: enumerate each instance in the Halide file
    //      -- e.g.,FFT GEMM IFFT
    //    - Task instances are contiguous:
    //      --- task instances are on the same input, same implementation, same iterator space
    //          ---- enumerate each instance in the Halide file
    //          ---- e.g., stencil stencil stencil stencil stencil [StencilChain]
    //      --- task instances are on the same input, same implementation, different iterator space (e.g., tiles)
    //          ---- re-roll these cases, only enumerate a single instance
    //               ----- lets the Halide scheduler design the tiles
    //          ---- e.g., stencil stencil stencil stencil stencil [OpenCV]
    // 3. Non-compliant tasks
    //    - when a non-compliant task is right in the middle of the pipeline, we need to do something with it
    //      -- highly-dependent on what the non-compliance is
    //         --- empty function: 
    //             ---- produces something
    //                  ----- consumes something: it probably shuffles memory around... this is trouble
    //                  ----- doesn't consume anything: it is an IO task, delete from the pipeline
    //             ---- doesn't produce anything
    //                  ----- consumes something: probably an IO task, delete from the pipeline
    //                  ----- doesn't consume anything: it is a "dead" task... be skeptical of these, did something go wrong in EP? If not, delete from the pipeline
    //         --- non-empty function:
    //             ---- produces something: we don't understand its type... we are in trouble
    //             ---- doesn't produce anything: 
    //                  ----- consumes something: probably an output task, delete from the pipeline
    //                  ----- doesn't consume anything: it is a "dead" task... be skeptical of these, did something go wrong in EP? If not, delete from the pipeline
    // 4. Pipeline parameters (GeneratorParams)
    //    - values that come from "outside" the pipeline
    //      -- they cannot be explained by any task's produced data
    //         --- this starts from the first task in the program ie if a given task in the program has a param that is not explained by any of its producers, that is a GeneratorParam
    //      -- sometimese these values are explained by non-task code; this can be a useful measure for which tasks need a header

    // before anything happens, we need to organize the pipeline in its producer-consumer order
    // this will allow us to refer to our producers when we generate halide expressions
    // [2024-01-26] for now we take the task graph and enumerate it according to its producer-consumer relationships
    // - this does not take into account multiple task instances
    auto exprOrder = instanceOrder;
    set<shared_ptr<Task>> pipelineInputs;
    // some post-processing of the pipeline
    // 1. Get rid of the input tasks
    for( const auto& entry : exprOrder )
    {
        if( taskLabels.contains(entry) )
        {
            if( taskLabels.at(entry).contains("Init") )
            {
                pipelineInputs.insert(entry);
            }
        }
    }
    for( const auto& r : pipelineInputs )
    {
        auto entry = std::find(exprOrder.begin(), exprOrder.end(), r);
        exprOrder.erase(entry);
    }

    // enumerate all task parameters that need to be declared as GeneratorParams
    // - these should be values that cannot be explained by any task in the pipeline
    //   -- we need to investigate each task for their TaskParameter(s), then enumerate them here
    set<shared_ptr<TaskParameter>> generatorParams;
    for( const auto& t : taskToExpr )
    {
        for( const auto& expr : t.second )
        {
            for( const auto& s : expr->getSymbols() )
            {
                if( const auto& param = static_pointer_cast<TaskParameter>(s) )
                {
                    generatorParams.insert(param);
                }
            }
        }
    }

    // TODO
    // 1. Not all reductions will be accumulate - generalize this
    // 2. Get the contained types of the input and output collections to the pipeline
    //    - what happens when these are user-defined types?
    // with the ordering of the tasks established, we can now build out the halide generator from the tasks

    // 1. start with the general stuff (Halide generators require some overhead... this is done here)
    string halideGenerator = "";
    halideGenerator += "#include <Halide.h>\n\nusing Halide::Generator;\n\n";
    // print any globals we need to declare for all tasks
    for( const auto& con : constants )
    {
        halideGenerator += con.second->dumpC()+";\n";
    }
    halideGenerator += "\n\n";
    // now start the generator definition
    halideGenerator += "class "+pipelineName+" : public Generator<"+pipelineName+"> {\npublic:\n";
    // 2. inject GeneratorParam(s) 
    for( const auto& param : generatorParams )
    {
        string typeStr;
        llvm::raw_string_ostream ty(typeStr);
        //PrintVal(param->getNode()->getVal());
        //param->getNode()->getVal()->getType()->print(ty);
        halideGenerator += "\tGeneratorParam<"+ty.str()+"> "+param->getName()+"{ \"+"+param->getName()+"\", "+to_string(0)+"};\n";
    }
    // 3. inject inputs
    for( const auto& expr : taskToExpr.at(exprOrder.front()) )
    {
        for( const auto& in : expr->getInputs() )
        {
            if( const auto& coll = dynamic_pointer_cast<Collection>(in) )
            {
                halideGenerator += "\tInput<Buffer<"+coll->getBP()->getContainedTypeString()+">> "+coll->getName()+"{\""+coll->getName()+"\", "+to_string(coll->getDimensions().size())+"};\n";
            }
        }
    }
    // 4. inject output
    string outputName = "";
    for( const auto& expr : taskToExpr.at(exprOrder.back()) )
    {
        if( const auto& coll = dynamic_pointer_cast<Collection>(expr->getOutput()) )
        {
            outputName = coll->getName();
            halideGenerator += "\tOutput<Buffer<"+coll->getBP()->getContainedTypeString()+">> "+coll->getName()+"{\""+coll->getName()+"\", "+to_string(coll->getDimensions().size())+"};\n";
        }
    }
    // 5. start generator
    halideGenerator += "\tvoid generate() {\n";
    // 5a. List all Vars (all dimensions used by the pipeline)
    set<shared_ptr<InductionVariable>> allVars;
    for( const auto& t : taskToExpr )
    {
        for( const auto& expr : t.second )
        {
            for( const auto& in : expr->getInputs() )
            {
                if( const auto& coll = dynamic_pointer_cast<Collection>(in) )
                {
                    for( const auto& var : coll->getIndices() )
                    {
                        for( const auto& dim : var->getDimensions() )
                        {
                            if( const auto& iv = dynamic_pointer_cast<InductionVariable>(dim) )
                            {
                                allVars.insert(iv);
                            }
                        }
                    }
                }
            }
        }
    }
    for( const auto& var : allVars )
    {
        halideGenerator += "\t\tVar "+var->getName()+"(\""+var->getName()+"\");\n";
    }
    // maps dimensions to the RDoms they are represented by
    // the keys in this map will be replaced by their values when the halide expressions are generated
    map<shared_ptr<Dimension>, shared_ptr<ReductionVariable>> DimToRDom;
    // 5b. print the expressions
    for( const auto& t : exprOrder )
    {
        for( const auto& expr : taskToExpr.at(t) )
        {
            // 5b.1 enumerate any reduction variables necessary
            set<shared_ptr<InductionVariable>> RDomDims;
            for( const auto& rv : expr->getRVs() )
            {
                for( const auto& dim : rv->getDimensions() )
                {
                    if( const auto& iv = dynamic_pointer_cast<InductionVariable>(dim) )
                    {
                        RDomDims.insert(iv);
                        DimToRDom[iv] = rv;
                    }
                }
                if( !RDomDims.empty() )
                {
                    vector<shared_ptr<InductionVariable>> ivs(RDomDims.begin(), RDomDims.end());
                    halideGenerator += "\t\tRDom "+rv->getName()+"(";
                    halideGenerator += to_string(ivs.front()->getSpace().min)+", "+to_string(ivs.front()->getSpace().max);
                    for( auto iv = next(ivs.begin()); iv != ivs.end(); iv++ )
                    {
                        halideGenerator += ", "+to_string((*iv)->getSpace().min)+", "+to_string((*iv)->getSpace().max);
                    }
                    halideGenerator += ");\n";
                }
            }
            vector<shared_ptr<InductionVariable>> exprDims;
            // 5b.2 enumerate all vars used in the expression
            if( const auto& outputColl = dynamic_pointer_cast<Collection>(expr->getOutput()) )
            {
                for( const auto& dim : outputColl->getDimensions() )
                {
                    if( const auto& iv = dynamic_pointer_cast<InductionVariable>(dim) )
                    {
                        exprDims.push_back(iv);
                    }
                }
            }
            else
            {
                throw CyclebiteException("Cannot print a task that doesn't have a collection as output!");
            }
            halideGenerator += "\t\tFunc "+expr->getName()+"(\""+expr->getName()+"\");\n";
            halideGenerator += "\t\t"+expr->getName()+"(";
            if( exprDims.size() )
            {
                halideGenerator += exprDims.front()->dumpHalide(DimToRDom);
                for( auto it = next(exprDims.begin()); it != exprDims.end(); it++ )
                {
                    halideGenerator += ", "+(*it)->dumpHalide(DimToRDom);
                }
            }
            halideGenerator += ") ";
            if( !expr->getRVs().empty() )
            {
                // assume its accumulate for now
                halideGenerator += "+= ";
            }
            else
            {
                halideGenerator += "= ";
            }
            halideGenerator += expr->dumpHalide(DimToRDom)+";\n\n";
        }
    }
    // 5c. Assign the last pipestage to "out"
    // we need the expression name of the last pipe stage
    halideGenerator += "\t\tFunc output(\"output\");\n";
    // it will have the same Vars as the last pipe stage
    vector<shared_ptr<InductionVariable>> outputDims;
    for( const auto& expr : taskToExpr.at(exprOrder.back()) )
    {
        // 5b.2 enumerate all vars used in the expression
        if( const auto& outputColl = dynamic_pointer_cast<Collection>(expr->getOutput()) )
        {
            for( const auto& dim : outputColl->getDimensions() )
            {
                if( const auto& iv = dynamic_pointer_cast<InductionVariable>(dim) )
                {
                    outputDims.push_back(iv);
                }
            }
        }
        halideGenerator += "\t\toutput(";
        string varString = "";
        if( outputDims.size() )
        {
            varString += outputDims.front()->dumpHalide(DimToRDom);
            for( auto it = next(outputDims.begin()); it != outputDims.end(); it++ )
            {
                varString += ", "+(*it)->dumpHalide(DimToRDom);
            }
        }
        halideGenerator += varString+") = "+expr->getName()+"("+varString+");\n";
    }
    // finally, out is assigned to output
    halideGenerator += "\t\t"+outputName+" = output;\n";

    // and close off the generator
    halideGenerator += "\t}\n};\n";
    halideGenerator += "HALIDE_REGISTER_GENERATOR("+pipelineName+", "+pipelineName+")";

    {
        ofstream generatorStream("Halide_generator.cpp");
        generatorStream << halideGenerator;
        generatorStream.close();
    }

    // now export the driver of the generator
    // some general includes
    string halideDriver = "#include <iostream>\n#include \"TimingLib.h\"\n\n#if HALIDE_AUTOSCHEDULE == 1\n#include \""+pipelineName+"_autoschedule_true_generated.h\"\n#endif\n#include \""+pipelineName+"_autoschedule_false_generated.h\"\n\n#include \"HalideBuffer.h\"\n\nusing namespace std;\nusing namespace Halide;\n\n";
    // start main
    halideDriver += "int main(int argc, char** argv) {\n";
    // we start with the number of input args there should be to the program
    int argc = 0;
    for( const auto& input : pipelineInputs ) {
        for( const auto& expr : taskToExpr.at(input) ) {
            if( const auto& coll = dynamic_pointer_cast<Collection>(expr->getOutput()) ) {
                argc++;
            }
        }
    }
    // account for the extra arg and thread count in the dynamic program arguments
    argc += 2;
    // this should be inputs only, the generator params are passed to the generator binary later
    halideDriver += "\tif( argc != "+to_string(argc)+" ) {\n\t\tcout << \"Usage: ";
    int inputId = 0;
    for( const auto& input : pipelineInputs )
    {
        for( const auto& expr : taskToExpr.at(input) )
        {
                if( const auto& coll = dynamic_pointer_cast<Collection>(expr->getOutput()) )
                {
                    halideDriver += "input"+to_string(inputId++)+"<"+coll->getBP()->getContainedTypeString()+"> ";
                }
        }
    }
    // we add a thread parameter to every driver
    halideDriver += "threads<int>\" << endl;\n\t\treturn 1;\n\t}\n\tint threads = stoi(argv["+to_string(argc-1)+"]);\n\tcout << \"Setting thread count to \"+to_string(threads) << endl;\n\thalide_set_num_threads(threads);\n\n";
    // prompt the user to inject any special input reading functions they use here
    halideDriver += "\t// USER: if you have any special reading functions for your inputs, inject them here and pass those parameters to the runtime buffers listed below (i.e., replace \"nullptr\" with your pointers)\n";
    // now enumerate the inputs
    inputId = 0;
    for( const auto& input : pipelineInputs ) {
        for( const auto& expr : taskToExpr.at(input) ) {
                if( const auto& coll = dynamic_pointer_cast<Collection>(expr->getOutput()) )
                {
                    halideDriver += "\tRuntime::Buffer<"+coll->getBP()->getContainedTypeString()+"> input"+to_string(inputId)+"( nullptr";
                    for( const auto& dim : coll->getDimensions() )
                    {
                        if( const auto& iv = dynamic_pointer_cast<InductionVariable>(dim) )
                        {
                            if( (iv->getSpace().max != static_cast<int>(STATIC_VALUE::UNDETERMINED)) && (iv->getSpace().max != static_cast<int>(STATIC_VALUE::INVALID)) )
                            {
                                if( (iv->getSpace().min != static_cast<int>(STATIC_VALUE::UNDETERMINED)) && (iv->getSpace().min != static_cast<int>(STATIC_VALUE::INVALID)) )
                                {
                                    halideDriver += ", "+to_string( abs(iv->getSpace().max - iv->getSpace().min) );
                                }
                                else
                                {
                                    // we can't determine what the dimension of this input is, so we leave it up to the user
                                    halideDriver += ", USER: fill in the size of this dimension for your input";
                                }
                            }
                            else
                            {
                                // we can't determine what the dimension of this input is, so we leave it up to the user
                                halideDriver += ", USER: fill in the size of this dimension for your input";
                            }
                        }
                    }
                    halideDriver += ");\n\tinput"+to_string(inputId++)+".allocate();\n";
                }
        }
    }
    // don't forget to allocate the output too
    int outputId = 0;
    for( const auto& expr : taskToExpr.at(exprOrder.back()) ) {
        if( const auto& coll = dynamic_pointer_cast<Collection>(expr->getOutput()) ) {
            halideDriver += "\tRuntime::Buffer<"+coll->getBP()->getContainedTypeString()+"> output"+to_string(outputId)+"( nullptr";
            for( const auto& dim : coll->getDimensions() )
            {
                if( const auto& iv = dynamic_pointer_cast<InductionVariable>(dim) )
                {
                    if( (iv->getSpace().max != static_cast<int>(STATIC_VALUE::UNDETERMINED)) && (iv->getSpace().max != static_cast<int>(STATIC_VALUE::INVALID)) )
                    {
                        if( (iv->getSpace().min != static_cast<int>(STATIC_VALUE::UNDETERMINED)) && (iv->getSpace().min != static_cast<int>(STATIC_VALUE::INVALID)) )
                        {
                            halideDriver += ", "+to_string( abs(iv->getSpace().max - iv->getSpace().min) );
                        }
                        else
                        {
                            // we can't determine what the dimension of this input is, so we leave it up to the user
                            halideDriver += ", USER: fill in the size of this dimension for your output";
                        }
                    }
                    else
                    {
                        // we can't determine what the dimension of this input is, so we leave it up to the user
                        halideDriver += ", USER: fill in the size of this dimension for your output";
                    }
                }
            }
            halideDriver += ");\n\toutput"+to_string(outputId++)+".allocate();\n";
        }
    }
    // now inject the calls to the generators (autoschedule and non-autoschedule)
    halideDriver += "\n#if HALIDE_AUTOSCHEDULE == 1\n\tdouble autotime = __TIMINGLIB_benchmark([&]() {\n\t\tauto out = "+pipelineName+"_autoschedule_true_generated(";
    inputId = 0;
    for( const auto& input : pipelineInputs ) {
        for( const auto& expr : taskToExpr.at(input) ) {
                if( const auto& coll = dynamic_pointer_cast<Collection>(expr->getOutput()) ) {
                    if( inputId > 0 ) halideDriver += ", ";
                    halideDriver += "input"+to_string(inputId++);
                }
        }
    }
    outputId = 0;
    for( const auto& expr : taskToExpr.at(exprOrder.back()) ) {
        if( const auto& coll = dynamic_pointer_cast<Collection>(expr->getOutput()) )
        {
            halideDriver += ", output"+to_string(outputId++);
        }
    }
    // do the host syncro thing with the output (if necessary, this only matters when pipelines are run on a GPU)
    halideDriver += ");\n";
    outputId = 0;
    for( const auto& expr : taskToExpr.at(exprOrder.back()) ) {
        if( const auto& coll = dynamic_pointer_cast<Collection>(expr->getOutput()) )
        {
            halideDriver += "\t\toutput"+to_string(outputId)+".device_sync();\n";
            halideDriver += "\t\toutput"+to_string(outputId++)+".copy_to_host();\n";
        }
    }
    halideDriver += "\t});\n#endif\n";
    // now repeat the whole thing for the autoscheduler
    halideDriver += "\n\tdouble time = __TIMINGLIB_benchmark([&]() {\n\t\tauto out = "+pipelineName+"_autoschedule_false_generated(";
    inputId = 0;
    for( const auto& input : pipelineInputs ) {
        for( const auto& expr : taskToExpr.at(input) ) {
                if( const auto& coll = dynamic_pointer_cast<Collection>(expr->getOutput()) ) {
                    if( inputId > 0 ) halideDriver += ", ";
                    halideDriver += "input"+to_string(inputId++);
                }
        }
    }
    outputId = 0;
    for( const auto& expr : taskToExpr.at(exprOrder.back()) ) {
        if( const auto& coll = dynamic_pointer_cast<Collection>(expr->getOutput()) )
        {
            halideDriver += ", output"+to_string(outputId++);
        }
    }
    halideDriver += ");\n";
    // do the host syncro thing with the output (if necessary, this only matters when pipelines are run on a GPU)
    outputId = 0;
    for( const auto& expr : taskToExpr.at(exprOrder.back()) ) {
        if( const auto& coll = dynamic_pointer_cast<Collection>(expr->getOutput()) )
        {
            halideDriver += "\t\toutput"+to_string(outputId)+".device_sync();\n";
            halideDriver += "\t\toutput"+to_string(outputId++)+".copy_to_host();\n";
        }
    }
    halideDriver += "\t});\n\tcout << \"Success!\" << endl;\n\treturn 0;\n}";
    // dump the driver
    {
        ofstream generatorStream("Halide_driver.cpp");
        generatorStream << halideDriver;
        generatorStream.close();
    }
}

void Cyclebite::Grammar::Export( const map<shared_ptr<Task>, vector<shared_ptr<Expression>>>& taskToExpr )
{
    try
    {
        // first, task name
        cout << endl;
        map<shared_ptr<Task>, set<string>> taskToLabel;
        // second, task optimization and export
        for( const auto& t : taskToExpr )
        {
            for( const auto& expr : t.second )
            {
    #ifdef DEBUG
                for( const auto& coll : expr->getCollections() )
                {
                    auto dotString = VisualizeCollection(coll);
                    ofstream tStream("Task"+to_string(expr->getTask()->getID())+"_Collection"+to_string(coll->getID())+".dot");
                    tStream << dotString;
                    tStream.close();
                }
    #endif
                auto parallelSpots = ParallelizeCycles( expr );
                auto vectorSpots   = VectorizeExpression( expr );
                auto exprLabel = MapTaskToName(expr, parallelSpots);
                taskToLabel[t.first].insert(exprLabel);
                spdlog::info("Cyclebite-Template Label: Task"+to_string(t.first->getID())+" -> "+exprLabel);
                OMPAnnotateSource(parallelSpots, vectorSpots);
                cout << endl;
            }
        }
        // third, export Halide
        exportHalide(taskToExpr, taskToLabel);
    }
    catch ( CyclebiteException& e )
    {
        spdlog::critical(e.what());
    }
}