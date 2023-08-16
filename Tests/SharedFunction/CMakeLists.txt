add_executable(SharedFunction SharedFunction.c)
InjectMarkov(SharedFunction)
InjectMemory(SharedFunction)

add_test(NAME SharedFunction_Markov_Profile COMMAND SharedFunction.markov.native)
set_tests_properties(SharedFunction_Markov_Profile PROPERTIES ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME SharedFunction_cartographer COMMAND newCartographer -i ${CMAKE_CURRENT_BINARY_DIR}/markov.bin -b $<TARGET_FILE:SharedFunction> -bi ${CMAKE_CURRENT_BINARY_DIR}/BlockInfo.json -d dot.dot -o ${CMAKE_CURRENT_BINARY_DIR}/kernel.json)
set_tests_properties(SharedFunction_cartographer PROPERTIES DEPENDS SharedFunction_Markov_Profile ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME SharedFunction_Memory_Profile COMMAND SharedFunction.memory.native)
set_tests_properties(SharedFunction_Memory_Profile PROPERTIES DEPENDS SharedFunction_cartographer ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME SharedFunction_Grammar COMMAND KernelGrammar -i ${CMAKE_CURRENT_BINARY_DIR}/instance.json -k ${CMAKE_CURRENT_BINARY_DIR}/kernel.json -b $<TARGET_FILE:SharedFunction> -bi ${CMAKE_CURRENT_BINARY_DIR}/BlockInfo.json -p ${CMAKE_CURRENT_BINARY_DIR}/markov.bin -o ${CMAKE_CURRENT_BINARY_DIR}/KernelGrammar.json ) 
set_tests_properties(SharedFunction_Grammar PROPERTIES DEPENDS SharedFunction_Memory_Profile ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/::${LLVM_INSTALL_PREFIX}/lib)