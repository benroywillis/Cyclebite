add_library(AtlasBackend SHARED HashTable/DashHashTable.c)

if(NOT WIN32)
    target_sources(AtlasBackend PRIVATE Utilities/Papi.c)
endif()

set(HASH_INC "${CMAKE_SOURCE_DIR}/Profile/Backend/HashTable/inc")
set(MARKOV_INC "${CMAKE_SOURCE_DIR}/Profile/Backend/Markov/inc")
set(MEMORY_INC "${CMAKE_SOURCE_DIR}/Profile/Backend/Memory/inc")
set(PRECISION_INC "${CMAKE_SOURCE_DIR}/Profile/Backend/Precision/inc")
set(UTILITIES_INC "${CMAKE_SOURCE_DIR}/Profile/Backend/Utilities/inc")
set(THREADSAFE_INC "${CMAKE_SOURCE_DIR}/Profile/Backend/ThreadSafe/inc")

add_subdirectory("HashTable")
add_subdirectory("Memory")
add_subdirectory("Precision")
add_subdirectory("Markov")
add_subdirectory("Utilities")
add_subdirectory("ThreadSafe")

set_target_properties(
    AtlasBackend PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    OUTPUT_NAME AtlasBackend
)

target_link_libraries(AtlasBackend ${llvm_libs} Util ZLIB::ZLIB nlohmann_json nlohmann_json::nlohmann_json)
target_include_directories(AtlasBackend PUBLIC ${HASH_INC} ${MARKOV_INC} ${MEMORY_INC} ${PRECISION_INC} ${UTILITIES_INC} ${THREADSAFE_INC})

install(TARGETS AtlasBackend
    LIBRARY DESTINATION lib
)

file(GLOB HH ${HASH_INC}/*.h)
install(FILES ${HH} DESTINATION "include/Profile/Backend/HashTable/")
file(GLOB MH  ${MARKOV_INC}/*.h)
install(FILES ${MH} DESTINATION "include/Profile/Backend/Markov/")
file(GLOB MEMH  ${MEMORY_INC}/*.h)
install(FILES ${MEMH} DESTINATION "include/Profile/Backend/Memory/")
file(GLOB PREH  ${PRECISION_INC}/*.h)
install(FILES ${PREH} DESTINATION "include/Profile/Backend/Precision/")
file(GLOB UH ${UTILITIES_INC}/*.h)
install(FILES ${UH} DESTINATION "include/Profile/Backend/Utilities/")
file(GLOB UH ${THREADSAFE_INC}/*.h)
install(FILES ${UH} DESTINATION "include/Profile/Backend/ThreadSafe/")