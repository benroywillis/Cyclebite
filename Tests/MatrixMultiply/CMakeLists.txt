add_executable(matmul main.c)
target_include_directories(matmul PRIVATE ${CMAKE_SOURCE_DIR}/Profile/Backend/Utilities/inc)
InjectTracer(matmul)
InjectMemory(matmul)

add_test(NAME matmul_Markov_Profile COMMAND matmul.markov.native)
set_tests_properties(matmul_Markov_Profile PROPERTIES ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME matmul_cartographer COMMAND newCartographer -i ${CMAKE_CURRENT_BINARY_DIR}/markov.bin -b $<TARGET_FILE:matmul> -bi ${CMAKE_CURRENT_BINARY_DIR}/BlockInfo.json -d dot.dot -o ${CMAKE_CURRENT_BINARY_DIR}/kernel.json)
set_tests_properties(matmul_cartographer PROPERTIES DEPENDS matmul_Markov_Profile ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME matmul_Memory_Profile COMMAND matmul.memory.native)
set_tests_properties(matmul_Memory_Profile PROPERTIES DEPENDS matmul_cartographer ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME matmul_Grammar COMMAND KernelGrammar -i ${CMAKE_CURRENT_BINARY_DIR}/instance.json -k ${CMAKE_CURRENT_BINARY_DIR}/kernel.json -b $<TARGET_FILE:matmul> -bi ${CMAKE_CURRENT_BINARY_DIR}/BlockInfo.json -p ${CMAKE_CURRENT_BINARY_DIR}/markov.bin -o ${CMAKE_CURRENT_BINARY_DIR}/KernelGrammar.json ) 
set_tests_properties(matmul_Grammar PROPERTIES DEPENDS matmul_Memory_Profile ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/::${LLVM_INSTALL_PREFIX}/lib)