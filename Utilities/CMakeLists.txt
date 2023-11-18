add_executable(kernelSourceMapper kernelSourceMapper.cpp)
target_link_libraries(kernelSourceMapper Graph)
set_target_properties(kernelSourceMapper
	PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" 
)
install(TARGETS kernelSourceMapper RUNTIME DESTINATION bin)

add_executable(CallGraph CallGraph.cpp)
target_link_libraries(CallGraph ${llvm_libs} Graph nlohmann_json nlohmann_json::nlohmann_json Util)
target_include_directories(CallGraph SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(CallGraph PRIVATE ${LLVM_DEFINITIONS})
set_target_properties(CallGraph PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(LoopInfoPass LoopInfoPass.cpp)
target_link_libraries(LoopInfoPass PRIVATE ${llvm_libs} nlohmann_json nlohmann_json::nlohmann_json Util)
target_include_directories(LoopInfoPass SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(LoopInfoPass PRIVATE ${LLVM_DEFINITIONS})
set_target_properties(LoopInfoPass
	PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" 
)
install(TARGETS LoopInfoPass RUNTIME DESTINATION bin)

add_executable(DFG DFG.cpp)
target_link_libraries(DFG ${LLVM} Graph nlohmann_json nlohmann_json::nlohmann_json Util)
target_include_directories(DFG SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_include_directories(DFG PRIVATE ${GRAPH_INC})
target_compile_definitions(DFG PRIVATE ${LLVM_DEFINITIONS})
set_target_properties(DFG
	PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" 
)
install(TARGETS DFG RUNTIME DESTINATION bin)

add_executable(DynamicCoverage DynamicCoverage.cpp)
target_link_libraries(DynamicCoverage ${LLVM} Graph nlohmann_json nlohmann_json::nlohmann_json Util)
target_include_directories(DynamicCoverage SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_include_directories(DynamicCoverage PRIVATE ${GRAPH_INC})
target_compile_definitions(DynamicCoverage PRIVATE ${LLVM_DEFINITIONS})
set_target_properties(DynamicCoverage
	PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" 
)
install(TARGETS DynamicCoverage RUNTIME DESTINATION bin)

add_executable(PrintCFG PrintCFG.cpp)
target_link_libraries(PrintCFG ${LLVM} Graph nlohmann_json nlohmann_json::nlohmann_json Util)
target_include_directories(PrintCFG SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_include_directories(PrintCFG PRIVATE ${GRAPH_INC})
target_compile_definitions(PrintCFG PRIVATE ${LLVM_DEFINITIONS})
set_target_properties(PrintCFG
	PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin" 
)
install(TARGETS PrintCFG RUNTIME DESTINATION bin)