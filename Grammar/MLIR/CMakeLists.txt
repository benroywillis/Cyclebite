include(HandleLLVMOptions)
add_subdirectory(inc)
set(CYCLEBITE_MLIR_INC "${CMAKE_SOURCE_DIR}/Grammar/MLIR/inc/")
include_directories(${PROJECT_BINARY_DIR}/Grammar/MLIR/inc/)

add_subdirectory(lib)
add_subdirectory(test)
add_subdirectory(opt)
