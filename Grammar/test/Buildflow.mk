## compilation config that is required for each test
# this variable dictates where the lib source files are, according to the test build folders
TEST_TO_LIB=../../lib/
# these are the grammar library source files, I can't yet get the lld linker to find all objects in libGrammar.so so for now static compilation has to be done
#ADDSOURCE+=$(CYCLEBITE_ROOT)/Grammar/Categorize.cpp $(CYCLEBITE_ROOT)/Grammar/Function.cpp $(CYCLEBITE_ROOT)/Grammar/InductionVariable.cpp $(CYCLEBITE_ROOT)/Grammar/BasePointer.cpp $(CYCLEBITE_ROOT)/Grammar/Collection.cpp $(CYCLEBITE_ROOT)/Grammar/Process.cpp $(CYCLEBITE_ROOT)/Grammar/Symbol.cpp $(CYCLEBITE_ROOT)/Grammar/Expression.cpp $(CYCLEBITE_ROOT)/Grammar/ReductionVariable.cpp $(CYCLEBITE_ROOT)/Grammar/Reduction.cpp $(CYCLEBITE_ROOT)/Grammar/Cycle.cpp $(CYCLEBITE_ROOT)/Grammar/ConstantSymbol.cpp $(CYCLEBITE_ROOT)/Grammar/Task.cpp $(CYCLEBITE_ROOT)/Grammar/ConstantFunction.cpp

OPFLAG=-O0
DEBUG=-g0
WARNING_OPTIONS=-Wno-c++17-extensions -Wno-deprecated-declarations
CXX=$(LLVM_INSTALL)bin/clang++
CXX_FLAGS=$(WARNING_OPTIONS) $(OPFLAG) $(DEBUG)
LLD=-fuse-ld=$(LLVM_INSTALL)bin/ld.lld -L$(CYCLEBITE_ROOT)build/lib/
A_LINKS=$(SPDLOG_INSTALL)lib/libspdlog.a $(FMT_INSTALL)lib/libfmt.a $(LLVM_INSTALL)lib/*.a
#D_LINKS=-lz -lpthread -l$(CYCLEBITE_ROOT)build/lib/libGraph.so -l$(CYCLEBITE_ROOT)build/lib/libGrammar.so
#D_LINKS=-lz -lpthread -lGraph -lGrammar
D_LINKS=-lz -lpthread -lGraph libGrammar.so
INCLUDE=-I$(CYCLEBITE_ROOT)Graph/inc/ -I$(CYCLEBITE_ROOT)Grammar/lib/inc/ -I$(CYCLEBITE_ROOT) -I$(LLVM_INSTALL)include/ -I$(LLVM_INSTALL)include/llvm-c/ -I$(SPDLOG_INSTALL)include/ -I$(SPDLOG_INSTALL)../fmt_x64-linux/include/ -I$(NLOHMANNJSON_INSTALL)include/

all : run

# drives the compilation of each source file in the grammar library into an object
GRAMMAR_SOURCES=$(wildcard $(TEST_TO_LIB)*.cpp)
LOCAL_GRAMMAR_SOURCES=$(patsubst $(TEST_TO_LIB)%,%,$(GRAMMAR_SOURCES))
LOCAL_GRAMMAR_OBJECTS=$(patsubst %.cpp,%.o,$(LOCAL_GRAMMAR_SOURCES))
LOCAL_GRAMMAR_NAMES=$(patsubst %.cpp,%,$(LOCAL_GRAMMAR_SOURCES))
define COMPILE_RULE = 
$(1).o : $(TEST_TO_LIB)$(1).cpp
	$(CXX) $(CXX_FLAGS) -c -fPIC $(INCLUDE) $(TEST_TO_LIB)$(1).cpp -o $(1).o
endef
objects : $(GRAMMAR_SOURCES)
$(foreach f,$(LOCAL_GRAMMAR_NAMES), $(eval $(call COMPILE_RULE,$f)))

libGrammar.so : objects
	$(CXX) -shared $(LLD) -I$(CYCLEBITE_ROOT)Grammar/inc/ $(LOCAL_GRAMMAR_OBJECTS) $(INCLUDE) -o $@

$(SOURCE).elf : $(SOURCE).cpp libGrammar.so
	$(CXX) $(LLD) $(CXX_FLAGS) $(INCLUDE) $(D_LINKS) $(A_LINKS) $< -o $@

run : $(SOURCE).elf
	LD_LIBRARY_PATH="$(CYCLEBITE_ROOT)build/lib/:$(CYCLEBITE_ROOT)Grammar/test/IV/" ./$(SOURCE).elf -i $(CASE_PATH)instance_$(CASE_NAME).json -k $(CASE_PATH)kernel_$(CASE_NAME).json -b $(CASE_PATH)$(CASE_NAME).bc -bi $(CASE_PATH)BlockInfo_$(CASE_NAME).json -p $(CASE_PATH)/$(CASE_NAME).bin -o $(CASE_PATH)/kg_$(CASE_NAME).json

.PHONY:

clean :
	rm -rf *.elf
