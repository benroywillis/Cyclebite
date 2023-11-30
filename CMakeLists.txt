cmake_minimum_required(VERSION 3.13)
project(Cyclebite VERSION 1.1.0)

SET(CMAKE_CXX_STANDARD 20)
SET(CMAKE_C_STANDARD 11)

find_package(LLVM 17 REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
llvm_map_components_to_libnames(llvm_libs support core irreader bitreader bitwriter transformutils lto)

# we fix the compiler toolchain to clang because we don't support gcc
set(CMAKE_C_COMPILER ${LLVM_INSTALL_PREFIX}/bin/clang)
set(CMAKE_CXX_COMPILER ${LLVM_INSTALL_PREFIX}/bin/clang++)
set(CMAKE_LINKER ${LLVM_INSTALL_PREFIX}/bin/ld.lld)
set(CMAKE_AR ${LLVM_INSTALL_PREFIX}/bin/llvm-ar)
set(CMAKE_ADDR2LINE ${LLVM_INSTALL_PREFIX}/bin/llvm-addr2line)
set(CMAKE_NM ${LLVM_INSTALL_PREFIX}/bin/llvm-nm)
set(CMAKE_OBJCOPY ${LLVM_INSTALL_PREFIX}/bin/llvm-objcopy)
set(CMAKE_OBJDUMP ${LLVM_INSTALL_PREFIX}/bin/llvm-objdump)
set(CMAKE_RANLIB ${LLVM_INSTALL_PREFIX}/bin/llvm-ranlib)
set(CMAKE_READELF ${LLVM_INSTALL_PREFIX}/bin/llvm-readelf)
set(CMAKE_STRIP ${LLVM_INSTALL_PREFIX}/bin/llvm-strip)

# we use vcpkg to provide these but any method should work
find_package(ZLIB REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

option(ENABLE_LINTER "Run linter" OFF)
if(${ENABLE_LINTER})
    find_program(TIDY NAMES clang-tidy-9 clang-tidy ENV "TIDY")
    if(${TIDY} STREQUAL "TIDY-NOTFOUND")
        message(FATAL_ERROR "clang-tidy not found! Necessary for linting!")
    else()
        message(STATUS "Found clang-tidy in ${TIDY}")
    endif()
    
    set(CMAKE_CXX_CLANG_TIDY 
        ${TIDY};
        --warnings-as-errors=*;
        --checks=readability-*,-readability-magic-numbers,performance-*,portability-*,modernize-*,-modernize-use-trailing-return-type,-readability-uppercase-literal-suffix;
        )
    set(CMAKE_C_CLANG_TIDY 
        ${TIDY};
        --warnings-as-errors=*;
        --checks=readability-*,-readability-magic-numbers,performance-*,portability-*,-clang-analyzer-security.insecureAPI.*,-readability-uppercase-literal-suffix;
        )
    add_compile_options(-Werror)
endif()

if(WIN32)
    add_compile_definitions(_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "DEBUG")
	message(STATUS "Setting DEBUG macro")
	add_compile_definitions(DEBUG)
elseif(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
	message(STATUS "Setting DEBUG macro")
	add_compile_definitions(DEBUG)
elseif(${CMAKE_BUILD_TYPE} STREQUAL "relwithdebinfo")
	message(STATUS "Setting DEBUG macro")
	add_compile_definitions(DEBUG)
    elseif(${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
	message(STATUS "Setting DEBUG macro")
	add_compile_definitions(DEBUG)
elseif(${CMAKE_BUILD_TYPE} STREQUAL "RELWITHDEBINFO")
	message(STATUS "Setting DEBUG macro")
	add_compile_definitions(DEBUG)
endif()

add_subdirectory("Util")
add_subdirectory("Graph")
add_subdirectory("cartographer")
add_subdirectory("Profile")
add_subdirectory("Utilities")
add_subdirectory("Grammar")

# responsible for injecting the tracer. Fairly fragile so be careful
function(InjectMarkov tar)
    target_compile_options(${tar} PRIVATE "-flto" "-g" "-mavx")
    set_target_properties(${tar} PROPERTIES LINK_FLAGS "-fuse-ld=${CMAKE_LINKER} -Wl,--plugin-opt=emit-llvm -fopenmp -mavx")
    add_custom_command(OUTPUT ${tar}.markov.bc
        #COMMAND ${LLVM_INSTALL_PREFIX}/bin/opt -enable-new-pm=0 -load $<TARGET_FILE:AtlasPasses> --Markov $<TARGET_FILE:${tar}> -o ${tar}.markov.bc
        COMMAND ${LLVM_INSTALL_PREFIX}/bin/opt --load-pass-plugin=$<TARGET_FILE:MarkovPass> --passes=Markov $<TARGET_FILE:${tar}> -o ${tar}.markov.bc
        DEPENDS $<TARGET_FILE:${tar}>
    )
    set_source_files_properties(
        ${tar}.markov.bc
        PROPERTIES
        EXTERNAL_OBJECT true
        GENERATED true
    )
    add_executable(${tar}.markov.native ${tar}.markov.bc)
    set_target_properties(${tar}.markov.native PROPERTIES LINKER_LANGUAGE CXX)
    #set_target_properties(${tar}.markov.native PROPERTIES LINK_FLAGS "-lpapi -lgomp -fopenmp -mavx")
    set_target_properties(${tar}.markov.native PROPERTIES LINK_FLAGS "-lgomp -fopenmp -mavx")
    target_link_libraries(${tar}.markov.native PRIVATE AtlasBackend)
endfunction()

function(InjectMemory tar)
    target_compile_options(${tar} PRIVATE "-flto" "-g" "-mavx")
    set_target_properties(${tar} PROPERTIES LINK_FLAGS "-fuse-ld=${CMAKE_LINKER} -Wl,--plugin-opt=emit-llvm -fopenmp -mavx")
    add_custom_command(OUTPUT ${tar}.memory.bc
        #COMMAND ${LLVM_INSTALL_PREFIX}/bin/opt -enable-new-pm=0 -load $<TARGET_FILE:AtlasPasses> --Memory $<TARGET_FILE:${tar}> -o ${tar}.memory.bc
        COMMAND ${LLVM_INSTALL_PREFIX}/bin/opt --load-pass-plugin=$<TARGET_FILE:MemoryPass> --passes=Memory $<TARGET_FILE:${tar}> -o ${tar}.memory.bc
        DEPENDS $<TARGET_FILE:${tar}>
    )
    set_source_files_properties(
        ${tar}.memory.bc
        PROPERTIES
        EXTERNAL_OBJECT true
        GENERATED true
    )
    add_executable(${tar}.memory.native ${tar}.memory.bc)
    set_target_properties(${tar}.memory.native PROPERTIES LINKER_LANGUAGE CXX)
    #set_target_properties(${tar}.markov.native PROPERTIES LINK_FLAGS "-lpapi -lgomp -fopenmp -mavx")
    set_target_properties(${tar}.markov.native PROPERTIES LINK_FLAGS "-lgomp -fopenmp -mavx")
    target_link_libraries(${tar}.memory.native PRIVATE AtlasBackend)
endfunction()

# our unit tests
option(ENABLE_TESTING "Build tests" ON)
if(${ENABLE_TESTING})
    enable_testing()
    add_subdirectory(Tests)
endif()

#find_package(Doxygen)
#if(DOXYGEN_FOUND)
#    configure_file(DoxygenLayout.xml ${CMAKE_CURRENT_BINARY_DIR}/DoxygenLayout.xml @ONLY)
#    configure_file(Doxyfile ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
#    add_custom_target(doc COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
#endif(DOXYGEN_FOUND)

# stolen from https://github.com/ttroy50/cmake-examples/blob/master/04-static-analysis/clang-format/cmake/modules/clang-format.cmake
# runs clang-format on the entire source tree
find_program(FORMAT NAMES clang-format ENV "FORMAT")
if(${FORMAT} STREQUAL "FORMAT-NOTFOUND")
    message(WARNING "clang-format not found. Disabling formatting target")
else()
    message(STATUS "Found clang-format in ${FORMAT}")
    set(CLANG_FORMAT_EXCLUDE_PATTERNS "build" "vcpkg")
    file(GLOB_RECURSE ALL_SOURCE_FILES *.cpp *.h *.c)
    foreach (SOURCE_FILE ${ALL_SOURCE_FILES})
        foreach (EXCLUDE_PATTERN ${CLANG_FORMAT_EXCLUDE_PATTERNS})
            string(FIND ${SOURCE_FILE} ${EXCLUDE_PATTERN} PROJECT_TRDPARTY_DIR_FOUND)
            if (NOT ${PROJECT_TRDPARTY_DIR_FOUND} EQUAL -1)
                list(REMOVE_ITEM ALL_SOURCE_FILES ${SOURCE_FILE})
            endif ()
        endforeach()
    endforeach ()

    add_custom_target(
        format
        COMMAND ${FORMAT}
        -style=file
        -i
        ${ALL_SOURCE_FILES}
    )
endif()
