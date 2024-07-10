add_mlir_dialect_library(MLIRCyclebite
        CyclebiteDialect.cpp
        CyclebiteOps.cpp

        ADDITIONAL_HEADER_DIRS 
        ${CYCLEBITE_MLIR_INC}

        DEPENDS
        MLIRCyclebiteOpsIncGen

	    LINK_LIBS PUBLIC
	    MLIRIR MLIRInferTypeOpInterface
	)
target_include_directories(MLIRCyclebite PRIVATE ${CYCLEBITE_MLIR_INC})