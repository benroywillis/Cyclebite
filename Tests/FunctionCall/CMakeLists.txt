add_executable(FunctionCall FunctionCall.c)
InjectMarkov(FunctionCall)
InjectMemory(FunctionCall)

add_test(NAME FunctionCall_Markov_Profile COMMAND FunctionCall.markov.native)
set_tests_properties(FunctionCall_Markov_Profile PROPERTIES ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME FunctionCall_cartographer COMMAND newCartographer -i ${CMAKE_CURRENT_BINARY_DIR}/markov.bin -b $<TARGET_FILE:FunctionCall> -bi ${CMAKE_CURRENT_BINARY_DIR}/BlockInfo.json -d dot.dot -o ${CMAKE_CURRENT_BINARY_DIR}/kernel.json)
set_tests_properties(FunctionCall_cartographer PROPERTIES DEPENDS FunctionCall_Markov_Profile ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME FunctionCall_Memory_Profile COMMAND FunctionCall.memory.native)
set_tests_properties(FunctionCall_Memory_Profile PROPERTIES DEPENDS FunctionCall_cartographer ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME FunctionCall_Grammar COMMAND KernelGrammar -i ${CMAKE_CURRENT_BINARY_DIR}/instance.json -k ${CMAKE_CURRENT_BINARY_DIR}/kernel.json -b $<TARGET_FILE:FunctionCall> -bi ${CMAKE_CURRENT_BINARY_DIR}/BlockInfo.json -p ${CMAKE_CURRENT_BINARY_DIR}/markov.bin -o ${CMAKE_CURRENT_BINARY_DIR}/KernelGrammar.json ) 
set_tests_properties(FunctionCall_Grammar PROPERTIES DEPENDS FunctionCall_Memory_Profile ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/::${LLVM_INSTALL_PREFIX}/lib)