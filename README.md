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
We recommend you go to the [Algorithms](https://github.com/benroywillis/Algorithms) repository for using Cyclebite. Its README will provide instructions on how the toolchain can be used with your LLVM Install. It will also provide a guide on what each outfile file from Cyclebite is and how to interpret its results.
