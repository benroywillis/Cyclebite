#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Frontend/OpenMP/OMPIRBuilder.h"
#include <spdlog/spdlog.h>

using namespace llvm;

// helpful new pass manager tutorials can be found at https://github.com/banach-space/llvm-tutor/blob/main/HelloWorld/HelloWorld.cpp

namespace 
{
    struct HelloWorld : PassInfoMixin<HelloWorld> {
        PreservedAnalyses run(Module& m, ModuleAnalysisManager& ) 
        {
            spdlog::info("Hello from new pass manager!");
            // args: no cross compilation = true, isGPU (false), target offloading (creating fat binary) = false, false for everything else
            /*auto obConfig   = OpenMPIRBuilderConfig( true, false, false, false );
            auto ompBuilder = OpenMPIRBuilder(m);
            spdlog::info("Just built the omp builder!");
            ompBuilder.setConfig(obConfig);
            ompBuilder.initialize();
            spdlog::info("Just configured and initialized omp builder!");*/
            return PreservedAnalyses::all();
        }

        // without setting this to true, all modules with "optnone" attribute are skipped
        static bool isRequired() { return true; }
    };

} // anonymous namespace

// new pass manager registration
llvm::PassPluginLibraryInfo getHelloWorldPluginInfo() 
{
    return {LLVM_PLUGIN_API_VERSION, "HelloWorld", LLVM_VERSION_STRING, 
        [](PassBuilder &PB) 
        {
            PB.registerPipelineParsingCallback( 
                [](StringRef Name, ModulePassManager& MPM, ArrayRef<PassBuilder::PipelineElement>) 
                {
                    if (Name == "HelloWorld") {
                        MPM.addPass(HelloWorld());
                        return true;
                    }
                    return false;
                }
            );
        }
    };
}

// guarantees this pass will be visible to opt when called
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() 
{
    return getHelloWorldPluginInfo();
}
