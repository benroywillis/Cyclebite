add_subdirectory("lib")

add_executable(KernelGrammar Grammar.cpp)
target_link_libraries(KernelGrammar Grammar Graph nlohmann_json nlohmann_json::nlohmann_json Util)
target_include_directories(KernelGrammar PRIVATE ${GRAPH_INC} "inc/")
target_include_directories(KernelGrammar PRIVATE ${GRAMMAR_INC} "inc/")
set_target_properties(KernelGrammar
	PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" 
)
install(TARGETS KernelGrammar RUNTIME DESTINATION bin)