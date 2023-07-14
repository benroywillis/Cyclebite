
LLVM_INSTALL=/mnt/heorot-10/bwilli46/Installs/LLVM9/release/
#LLVM_INSTALL=/mnt/heorot-10/bwilli46/Installs/LLVM13/release/
CC=$(LLVM_INSTALL)bin/clang
CXX=$(LLVM_INSTALL)bin/clang++
SO_PATH=LD_LIBRARY_PATH="$(LD_LIBRARY_PATH):$(LLVM_INSTALL)lib/"

OPFLAG?=-O0
DEBUG?=-g3
#DEBUG?=-g3 -DDEBUG

SOURCE?=test_Queue_HT
ADDSOURCE=../Task.cpp ../ThreadSafeQueue.cpp ../AtomicQueue.cpp ../../HashTable/DashHashTable.c
LOG?=tmp

D_LINKS=-lpthread -lgomp -fopenmp 
INCLUDE=-I../inc -I../../HashTable/inc/
CXXFLAGS += -Wno-c++17-extensions

all : $(SOURCE).elf

$(SOURCE).elf : $(SOURCE).cpp $(ADDSOURCE)
	$(CXX) $(CXXFLAGS) $(OPFLAG) $(DEBUG) $(INCLUDE) $(D_LINKS) $(ADDSOURCE) $< -o $@

run : $(SOURCE).elf
	$(SO_PATH) ./$< | tee debug_output/$(LOG).txt

gdb : $(SOURCE).elf
	$(SO_PATH) gdb $(SOURCE).elf

.PHONY :

clean :
	rm *.elf tmp.txt
