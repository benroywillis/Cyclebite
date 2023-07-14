add_executable(1DCondition 1DCondition.c)
InjectTracer(1DCondition)
InjectMemory(1DCondition)

add_test(NAME 1DCondition_Markov_Profile COMMAND 1DCondition.markov.native)
message(STATUS ${CMAKE_CURRENT_BINARY_DIR}/../../build/lib/)
set_tests_properties(1DCondition_Markov_Profile PROPERTIES ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME 1DCondition_cartographer COMMAND newCartographer -i ${CMAKE_CURRENT_BINARY_DIR}/markov.bin -b $<TARGET_FILE:1DCondition> -bi ${CMAKE_CURRENT_BINARY_DIR}/BlockInfo.json -d dot.dot -o ${CMAKE_CURRENT_BINARY_DIR}/kernel.json)
set_tests_properties(1DCondition_cartographer PROPERTIES DEPENDS 1DCondition_Markov_Profile ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME 1DCondition_Memory_Profile COMMAND 1DCondition.memory.native)
set_tests_properties(1DCondition_Memory_Profile PROPERTIES DEPENDS 1DCondition_cartographer ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/::${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME 1DCondition_Grammar COMMAND KernelGrammar -i ${CMAKE_CURRENT_BINARY_DIR}/instance.json -k ${CMAKE_CURRENT_BINARY_DIR}/kernel.json -b $<TARGET_FILE:1DCondition> -bi ${CMAKE_CURRENT_BINARY_DIR}/BlockInfo.json -p ${CMAKE_CURRENT_BINARY_DIR}/markov.bin -o ${CMAKE_CURRENT_BINARY_DIR}/KernelGrammar.json ) 
set_tests_properties(1DCondition_Grammar PROPERTIES DEPENDS 1DCondition_Memory_Profile ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/::${LLVM_INSTALL_PREFIX}/lib)