add_executable(bubbleSort bubbleSort.c)
target_include_directories(bubbleSort PRIVATE ${CMAKE_SOURCE_DIR}/Profile/Backend/Markov/inc)
InjectMarkov(bubbleSort)
InjectMemory(bubbleSort)

add_test(NAME bubbleSort_Markov_Profile COMMAND bubbleSort.markov.native 512)
set_tests_properties(bubbleSort_Markov_Profile PROPERTIES ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME bubbleSort_cartographer COMMAND newCartographer -i ${CMAKE_CURRENT_BINARY_DIR}/markov.bin -b $<TARGET_FILE:bubbleSort> -bi ${CMAKE_CURRENT_BINARY_DIR}/BlockInfo.json -d dot.dot -o ${CMAKE_CURRENT_BINARY_DIR}/kernel.json)
set_tests_properties(bubbleSort_cartographer PROPERTIES DEPENDS bubbleSort_Markov_Profile ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME bubbleSort_Memory_Profile COMMAND bubbleSort.memory.native 512)
set_tests_properties(bubbleSort_Memory_Profile PROPERTIES DEPENDS bubbleSort_cartographer ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME bubbleSort_Grammar COMMAND KernelGrammar -i ${CMAKE_CURRENT_BINARY_DIR}/instance.json -k ${CMAKE_CURRENT_BINARY_DIR}/kernel.json -b $<TARGET_FILE:bubbleSort> -bi ${CMAKE_CURRENT_BINARY_DIR}/BlockInfo.json -p ${CMAKE_CURRENT_BINARY_DIR}/markov.bin -o ${CMAKE_CURRENT_BINARY_DIR}/KernelGrammar.json ) 
set_tests_properties(bubbleSort_Grammar PROPERTIES DEPENDS bubbleSort_Memory_Profile ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/::${LLVM_INSTALL_PREFIX}/lib)