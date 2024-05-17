# Cyclebite

[Cyclebite](https://ieeexplore.ieee.org/document/10301361) is a program analysis toolchain that extracts the task graph from unstructured compute-programs automatically. It uses the [LLVM project](https://github.com/llvm/llvm-project) to profile programs dynamically, extract coarse-grained task candidates from that profile's state transitions, dynamically localizes the epochs of the application, and exports a directed acyclic graph describing the structure of the application. The nodes of that graph are groups of basic blocks and edges between those nodes are communication patterns.

Cyclebite-Template is a follow-on work of Cyclebite that characterizes the tasks in the extracted Cyclebite task graph. Cyclebite-Template accepts the task graph from Cyclebite as input and extracts the parallel pattern from each task in the task graph. A label describing the parallel pattern of that task (according to [DeLite's labels](https://dl.acm.org/doi/abs/10.1145/2584665)) is assigned to each task. Finally, Cyclebite-Template exports a [Halide](https://github.com/halide/Halide) program of the input task graph.

Several test programs and a toolchain build flow can be found in the [Algorithms](https://github.com/benroywillis/Algorithms) repository. See that repo's README on how the build flow works and how you can structure your own C/C++ project with Cyclebite and Cyclebite-Template.

## Building
Cyclebite requires cmake version 3.13 or higher. You can run the test suite with the `test` target and generate the documentation with the `doc` target.

### Dependencies
* [LLVM17](https://llvm.org/)v17
* [nlohmann-json](https://github.com/nlohmann/json)v3.7.3
* [zlib](https://www.zlib.net/)v1.2.11
* [spdlog](https://github.com/gabime/spdlog)v1.3.0

#### Building LLVM

The current development version of Cyclebite uses LLVM17 to both link against and build its source code. It is recommended that you use the same version for your own development. YOU MUST USE THE SAME INSTALL OF LLVM TO BOTH COMPILE THE REPOSITORY AND LINK THE REPOSITORY AGAINST. This is to ensure that the legacy LLVM passes will have all their symbols defined when running opt passes.

We recommend you build llvm from source - this is the only way to ensure all submodules will be present and the correct version (clang, lld, openmp).  
`wget <link-to-llvm17>`  
`tar -xvf llvm-project-llvmorg-17.tar.gz`  
`cd llvm-project-llvmorg-17`  
`mkdir build ; cd build`  
`cmake ../llvm/ -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/path/to/install/place/ -DLLVM_ENABLE_PROJECTS="clang;lld;openmp" -DLLVM_ENABLE_RTTI=ON ; ninja -j<threads> ; ninja test ; ninja install`  
(we find that memory usage is ~1.0GB/thread throughout the compilation process, so adjust your thread count according to available memory)  
(we recommend you build both -DCMAKE_BUILD_TYPE=Debug and -DCMAKE_BUILD_TYPE=Release)  

After this process, set your llvm build to be the default compiler for your user space  
`vi ~/.bashrc`  
add the following lines  
`export CC=/path/to/Installs/LLVM17/release/bin/clang`  
`export CXX=/path/to/Installs/LLVM17/release/bin/clang++`  
`export LLD=/path/to/Installs/LLVM17/release/bin/ld.lld`  

#### Build and link dependencies using VCPKG (easy way)
A long time ago, [vcpkg](https://github.com/microsoft/vcpkg) was used to build the dependencies of this repository, and specific things relating to this package manager were injected into the CMake build flow and source code of Cyclebite. For the sake of simplicity and convenience, it is recommended to use vcpkg to install the dependencies, then use their cmake toolchain file to import them into the Cyclebite buildflow.

Installing, bootstrapping and installing packages using vcpkg has been heavily refined to make it pretty easy to use. Simply clone their repository and run the bootstrap script. After this, install four dependencies using the vcpkg binary:  

`git clone git@github.com:microsoft/vcpkg.git`  
`./bootstrap-vcpkg.sh`  
`./vcpkg install zlib nlohmann-json spdlog`  

When configuring the Cyclebite build flow, point to vcpkg's buildscript and CMake will find all dependencies you have installed:  
`-DCMAKE_TOOLCHAIN_FILE=${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake`  

#### Build and link dependencies manually (hard way)
Currently Cyclebite does not support custom installs. Its buildflow and source code are dependent on the configurations and custom build parameters of vcpkg. If you decide to go this route, there will be both CMake and source modifications required.

The library dependency versions have not been well explored, so compatibility may not be supported outside the versions currently being used for development. The current development effort has built some dependencies from source (LLVM, nlohmann-json), installed the headers (spdlog), or used the local package manager (apt repository installs for zlib and papi).

For each dependency you install, you must have the CMake config files handy. Point the CMake variable for each dependency to the folder that contains its <LIBRARY_NAME>Config.cmake file:  
`-DLLVM_DIR=${LLVM_INSTALL_PREFIX}/lib/cmake/llvm/`  
`-Dnlohmann_json_DIR=${NLOHMANN_JSON_INSTALL_PREFIX}/lib/cmake/nlohmann_json/`  
`-Dindica_DIR=${INDICATORS_INSTALL_PREFIX}/lib/cmake/indica/`  
`-Dspdlog_DIR=${SPDLOG_INSTALL_PREFIX}/lib/cmake/spdlog/`  

When linking against dependency installs, an example build command:  
`mkdir build ; cd build ; $CMAKE ../ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTING_LONG=ON -DLLVM_DIR=/mnt/heorot-10/bwilli46/Installs/LLVM9/install-release/lib/cmake/llvm/ -Dnlohmann_json_DIR=/mnt/heorot-10/bwilli46/Installs/nlohmann_json_3.7.3/release/lib/cmake/nlohmann_json/ -Dindica_DIR=/mnt/heorot-10/bwilli46/Installs/indicators/release/lib/cmake/indica/ -Dspdlog_DIR=/mnt/heorot-10/bwilli46/Installs/spdlog1.3.0/release/lib/cmake/spdlog/ ; ninja ; ninja test`  

### Example Cyclebite build 
`mkdir build ; cd build ; $CMAKE ../ -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/path/to/Cyclebite/debug/ -DLLVM_DIR=/path/to/Installs/LLVM17/release/lib/cmake/llvm/ -DCMAKE_TOOLCHAIN_FILE=${VCPKG_INSTALL_PREFIX}/scripts/buildsystems/vcpkg.cmake ; ninja test ; ninja install`

## Usage
We recommend you go to the [Algorithms](https://github.com/benroywillis/Algorithms) repository for using Cyclebite. Its README will provide instructions on how the toolchain can be used with your LLVM Install. It will also provide a guide on what each outfile file from Cyclebite is and how to interpret their results.

The main output file from cartographer is kernel.json. This file contains a dictionary of many pieces of information, the most important being the "Kernels" dictionary. Inside "Kernels" are keys of IDs that belong to each individual kernel. Within a kernel ID is the "Blocks" list that contains all unique block IDs that belong to this kernel. Several other pieces of information, like performance intrinsics, the dynamic "Nodes" that represented the kernel in the segmentation algorithm, and others describe interesting characteristics about the kernel.

If hotcode detection is enabled, cartographer will output two additional kernel files: one with suffix .json_HC and another with suffix .json_HL. HC stands for hotcode, and its kernel file contains kernels constituting "hotblocks" from the profile. By default, the hotcode detection algorithm will sort blocks from greatest frequency to least, then gather all hot blocks until 95% of the total basic block execution frequency has been explained. To adjust this threshold, use the `-ht` option. HL stands for hotloop. A hotloop is a static loop that has at least one hot block in it. These two program segmentation schemes are intended to simulate state-of-the-art program segmentation techniques used in the computer architecture field.

If the repository is compiled with configuration `-DCMAKE_BUILD_TYPE=Debug`, cartographer will output several additional files, many of which will have suffix `.dot`. These files encode the control flow graph of the program being analyzed at certain stages of the segmentation algorithm. These files can be converted in .svg graphics using [GraphViz](https://pypi.org/project/graphviz/). 

* `StaticControlGraph.dot` is a rendering of the input profile before any processing.
* `LastGraphPrint.dot` is a control flow graph with a highlighted subgraph of the last function the cartographer attempted to inline.
* `LastFunctionInlineTransform.dot` is a control flow graph of the result of the last function inline transform. A function inline transform occurs when a function was used at more than one callsite during the execution of the profile. This disambiguates loops in the profile of the program from being merged because of shared functions.
* `LastTransform.dot` is a control flow graph of the result of the last CFG transform performed by the cartographer.
* `LastTrivialTransform.dot` is a control flow graph of the result of the last trivial transform of two nodes in the CFG. A trivial transform is one in which two serial nodes with certain probability are combined into a single node.
* `LastVirtualizationTransform.dot` is a control flow graph of the result of the last complex transform of two or more nodes in the CFG. Complex transforms are transforms of subgraphs in the control flow whose fan-in and fan-out are bottlenecked to two unique nodes (also known as "supergraphs"). This subgraph is not allowed to contain cycles.
* `TransformedStaticControlGraph.dot` is the CFG of the program after all transforms have been applied and before loop segmentation has begun.
* `TransformedStaticControlGraph_\d.dot` is the CFG of the program after \d iterations of the loop segmentation algorithm has taken place.
* `dot_<Program>.dot` is the final segmentation result of the cartographer. Dashed edges denote hierarchical kernel relationships, pointing from child to parent. 
