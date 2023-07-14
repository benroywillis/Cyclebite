# grammar object objects
set(SOURCES Categorize.cpp Function.cpp InductionVariable.cpp BasePointer.cpp Collection.cpp Process.cpp Symbol.cpp Expression.cpp ReductionVariable.cpp Reduction.cpp Cycle.cpp ConstantSymbol.cpp Task.cpp ConstantFunction.cpp)

add_library(Grammar SHARED ${SOURCES})
set_target_properties(
    Grammar PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    OUTPUT_NAME Grammar
)

target_link_libraries(Grammar ${LLVM} Util nlohmann_json nlohmann_json::nlohmann_json)
target_include_directories(Grammar PUBLIC "inc/")
if(WIN32)
    target_compile_options(Grammar PRIVATE -W3 -Wextra -Wconversion)
else()
    target_compile_options(Grammar PRIVATE -Wall -Wextra -Wconversion)
endif()

install(TARGETS Grammar
    LIBRARY DESTINATION lib
)

set(GRAMMAR_INC "${CMAKE_SOURCE_DIR}/Grammar/inc/")

file(GLOB GR ${GRAMMAR_INC}/*.h)
install (FILES ${GR} DESTINATION "include/Grammar")

# driver
add_executable(KernelGrammar Grammar.cpp)
target_link_libraries(KernelGrammar Grammar Graph nlohmann_json nlohmann_json::nlohmann_json Util)
target_include_directories(KernelGrammar PRIVATE ${GRAPH_INC} "inc/")
target_include_directories(KernelGrammar PRIVATE ${GRAMMAR_INC} "inc/")
set_target_properties(KernelGrammar
	PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" 
)
install(TARGETS KernelGrammar RUNTIME DESTINATION bin)