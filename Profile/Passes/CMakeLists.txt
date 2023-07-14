add_library(AtlasPasses MODULE)

set(MARKOV_INC "Markov/inc")
set(LOOP_INC "Loop/inc")
set(MEMORY_INC "Memory/inc")
set(PRECISION_INC "Precision/inc")
set(PRECISION_B_INC ${CMAKE_SOURCE_DIR}/Profile/)
set(MEMORY_B_INC ${CMAKE_SOURCE_DIR}/Profile/Backend/Memory/inc)
set(PAPI_INC "Papi/inc")
set(TRACE_INC "Trace/inc")
set(UTILITIES_INC "Utilities/inc")

add_subdirectory("Loop")
add_subdirectory("Markov")
add_subdirectory("Memory")
add_subdirectory("Precision")
add_subdirectory("Papi")
add_subdirectory("Trace")
add_subdirectory("Utilities")
message(STATUS ${CMAKE_SOURCE_DIR}/Profile/)
target_link_libraries(AtlasPasses PRIVATE nlohmann_json nlohmann_json::nlohmann_json AtlasUtil)
target_compile_definitions(AtlasPasses PRIVATE ${LLVM_DEFINITIONS})
if(WIN32)
    target_compile_options(AtlasPasses PRIVATE -W4  -Wextra  -Wconversion)
else()
    target_compile_options(AtlasPasses PRIVATE -Wall  -Wextra  -Wconversion)
endif()
set_target_properties(AtlasPasses
    PROPERTIES
    PREFIX ""
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

target_include_directories(AtlasPasses PRIVATE ${MARKOV_INC} ${LOOP_INC} ${MEMORY_INC} ${PRECISION_INC} ${PRECISION_B_INC} ${MEMORY_B_INC} ${PAPI_INC} ${TRACE_INC} ${UTILITIES_INC})
install(TARGETS AtlasPasses
    LIBRARY DESTINATION lib
)