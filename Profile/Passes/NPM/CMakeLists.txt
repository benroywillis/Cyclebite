# for some unknown reason, opt will not recognize the passes that exist outside the first listed source file
add_library(HelloWorldPass MODULE)
target_sources(HelloWorldPass PRIVATE HelloWorld.cpp)
target_link_libraries(HelloWorldPass PRIVATE nlohmann_json Util)
target_compile_definitions(HelloWorldPass PRIVATE ${LLVM_DEFINITIONS})
set_target_properties(HelloWorldPass PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
target_include_directories(HelloWorldPass PRIVATE ${MARKOV_INC} ${ANNOTATE_INC} ${UTILITIES_INC})
install(TARGETS HelloWorldPass LIBRARY DESTINATION lib)