add_library(MarkovPass MODULE)
target_sources(MarkovPass PRIVATE Markov.cpp ../Utilities/Functions.cpp)
target_link_libraries(MarkovPass PRIVATE nlohmann_json Util)
target_compile_definitions(MarkovPass PRIVATE ${LLVM_DEFINITIONS})
set_target_properties(MarkovPass PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
target_include_directories(MarkovPass PRIVATE ${UTILITIES_INC})
install(TARGETS MarkovPass LIBRARY DESTINATION lib)