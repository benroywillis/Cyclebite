add_executable(1DBlur 1DBlur.c)
InjectMarkov(1DBlur)
InjectMemory(1DBlur)

add_test(NAME 1DBlur_Markov_Profile COMMAND  1DBlur.markov.native)
set_tests_properties(1DBlur_Markov_Profile PROPERTIES ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME 1DBlur_cartographer COMMAND newCartographer -i ${CMAKE_CURRENT_BINARY_DIR}/markov.bin -b $<TARGET_FILE:1DBlur> -bi ${CMAKE_CURRENT_BINARY_DIR}/BlockInfo.json -d dot.dot -o ${CMAKE_CURRENT_BINARY_DIR}/kernel.json)
set_tests_properties(1DBlur_cartographer PROPERTIES DEPENDS 1DBlur_Markov_Profile ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME 1DBlur_Memory_Profile COMMAND 1DBlur.memory.native)
set_tests_properties(1DBlur_Memory_Profile PROPERTIES DEPENDS 1DBlur_cartographer ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/:${LLVM_INSTALL_PREFIX}/lib)

add_test(NAME 1DBlur_Grammar COMMAND KernelGrammar -i ${CMAKE_CURRENT_BINARY_DIR}/instance.json -k ${CMAKE_CURRENT_BINARY_DIR}/kernel.json -b $<TARGET_FILE:1DBlur> -bi ${CMAKE_CURRENT_BINARY_DIR}/BlockInfo.json -p ${CMAKE_CURRENT_BINARY_DIR}/markov.bin -o ${CMAKE_CURRENT_BINARY_DIR}/KernelGrammar.json ) 
set_tests_properties(1DBlur_Grammar PROPERTIES DEPENDS 1DBlur_Memory_Profile ENVIRONMENT LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}/../../lib/::${LLVM_INSTALL_PREFIX}/lib)