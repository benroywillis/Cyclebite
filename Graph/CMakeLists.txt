set(SOURCES Dijkstra.cpp GraphNode.cpp ImaginaryNode.cpp ControlNode.cpp IO.cpp Transforms.cpp MLCycle.cpp ControlBlock.cpp DataValue.cpp Arg.cpp Inst.cpp VirtualNode.cpp GraphEdge.cpp UnconditionalEdge.cpp CallEdge.cpp ConditionalEdge.cpp ImaginaryEdge.cpp VirtualEdge.cpp CallGraphNode.cpp ReturnEdge.cpp Graph.cpp ControlGraph.cpp DataGraph.cpp CallGraph.cpp CallGraphEdge.cpp CallNode.cpp)

add_library(Graph SHARED ${SOURCES})
set_target_properties(
    Graph PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    OUTPUT_NAME Graph
)

target_link_libraries(Graph ${llvm_libs} Util ZLIB::ZLIB nlohmann_json nlohmann_json::nlohmann_json)
target_include_directories(Graph PUBLIC "inc/" ${TRACE_INC})
if(WIN32)
    target_compile_options(Graph PRIVATE -W3 -Wextra -Wconversion)
else()
    target_compile_options(Graph PRIVATE -Wall -Wextra -Wconversion)
endif()

install(TARGETS Graph
    LIBRARY DESTINATION lib
)

set(GRAPH_INC "${CMAKE_SOURCE_DIR}/Graph/inc/")
message(STATUS ${GRAPH_INC})

file(GLOB GR ${GRAPH_INC}/*.h)
install (FILES ${GR} DESTINATION "include/Graph")