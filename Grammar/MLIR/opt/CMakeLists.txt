get_property(dialect_libs GLOBAL PROPERTY MLIR_DIALECT_LIBS)
get_property(conversion_libs GLOBAL PROPERTY MLIR_CONVERSION_LIBS)
set(LIBS
        ${dialect_libs}
        ${conversion_libs}
        MLIROptLib
        MLIRCyclebite
        )
add_llvm_executable(cyclebite-opt opt.cpp)

llvm_update_compile_flags(cyclebite-opt)
target_link_libraries(cyclebite-opt PRIVATE ${LIBS})
target_include_directories(cyclebite-opt PRIVATE ${CYCLEBITE_MLIR_INC})