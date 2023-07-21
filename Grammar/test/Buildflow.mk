## compilation config that is required for each test
# these are the grammar library source files, I can't yet get the lld linker to find all objects in libGrammar.so so for now static compilation has to be done
ADDSOURCE+=$(CYCLEBITE_ROOT)/Grammar/Categorize.cpp $(CYCLEBITE_ROOT)/Grammar/Function.cpp $(CYCLEBITE_ROOT)/Grammar/InductionVariable.cpp $(CYCLEBITE_ROOT)/Grammar/BasePointer.cpp $(CYCLEBITE_ROOT)/Grammar/Collection.cpp $(CYCLEBITE_ROOT)/Grammar/Process.cpp $(CYCLEBITE_ROOT)/Grammar/Symbol.cpp $(CYCLEBITE_ROOT)/Grammar/Expression.cpp $(CYCLEBITE_ROOT)/Grammar/ReductionVariable.cpp $(CYCLEBITE_ROOT)/Grammar/Reduction.cpp $(CYCLEBITE_ROOT)/Grammar/Cycle.cpp $(CYCLEBITE_ROOT)/Grammar/ConstantSymbol.cpp $(CYCLEBITE_ROOT)/Grammar/Task.cpp $(CYCLEBITE_ROOT)/Grammar/ConstantFunction.cpp

OPFLAG=-O0
DEBUG=-g0
WARNING_OPTIONS=-Wno-c++17-extensions -Wno-deprecated-declarations
CXX=$(LLVM_INSTALL)bin/clang++
CXX_FLAGS=$(WARNING_OPTIONS) $(OPFLAG) $(DEBUG)
LLD=-fuse-ld=$(LLVM_INSTALL)bin/ld.lld -L$(CYCLEBITE_ROOT)build/lib/
A_LINKS=$(SPDLOG_INSTALL)lib/libspdlog.a $(FMT_INSTALL)lib/libfmt.a $(LLVM_INSTALL)lib/*.a
#D_LINKS=-lz -lpthread -l$(CYCLEBITE_ROOT)build/lib/libGraph.so -l$(CYCLEBITE_ROOT)build/lib/libGrammar.so
D_LINKS=-lz -lpthread -lGraph -lGrammar
INCLUDE=-I$(CYCLEBITE_ROOT)Graph/inc/ -I$(CYCLEBITE_ROOT)Grammar/inc/ -I$(CYCLEBITE_ROOT) -I$(LLVM_INSTALL)include/ -I$(LLVM_INSTALL)include/llvm-c/ -I$(SPDLOG_INSTALL)include/ -I$(SPDLOG_INSTALL)../fmt_x64-linux/include/ -I$(NLOHMANNJSON_INSTALL)include/

all : run

$(SOURCE).elf : $(SOURCE).cpp
	$(CXX) $(LLD) $(CXX_FLAGS) $(INCLUDE) $(D_LINKS) $(A_LINKS) $(ADDSOURCE) $< -o $@

run : $(SOURCE).elf
	LD_LIBRARY_PATH=$(CYCLEBITE_ROOT)build/lib/ ./$(SOURCE).elf -i $(CASE_PATH)instance_$(CASE_NAME).json -k $(CASE_PATH)kernel_$(CASE_NAME).json -b $(CASE_PATH)$(CASE_NAME).bc -bi $(CASE_PATH)BlockInfo_$(CASE_NAME).json -p $(CASE_PATH)/$(CASE_NAME).bin -o $(CASE_PATH)/kg_$(CASE_NAME).json

.PHONY:

clean :
	rm -rf *.elf
