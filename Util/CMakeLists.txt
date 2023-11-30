add_library(Util INTERFACE)

target_compile_definitions(Util INTERFACE ${LLVM_DEFINITIONS})
target_include_directories(Util SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS})
# CMAKE_SOURCE_DIR included for headers from newCartographer objects defined in cartographer/new/
target_include_directories(Util INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/Graph/inc/)
target_link_libraries(Util INTERFACE spdlog::spdlog_header_only ZLIB::ZLIB)