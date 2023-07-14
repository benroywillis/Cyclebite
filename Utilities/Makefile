#LLVM_INSTALL=/usr/lib/llvm-9/
LLVM_INSTALL=/mnt/heorot-10/bwilli46/LLVM12/install-release/
#COMPILER_INSTALL=$(LLVM_INSTALL)
COMPILER_INSTALL=/usr/
PAMUL_ROOT=../
SPDLOG_INSTALL=/mnt/nobackup-11/bwilli46/vcpkg/packages/spdlog_x64-linux/
FMT_INSTALL=/mnt/nobackup-11/bwilli46/vcpkg/packages/fmt_x64-linux/
NLOHMANNJSON_INSTALL=/mnt/nobackup-11/bwilli46/vcpkg/packages/nlohmann-json_x64-linux/

SOURCE=LoopInfoPass
#ADDSOURCE=$(PAMUL_ROOT)cartographer/new/VKNode.cpp $(PAMUL_ROOT)cartographer/new/Transforms.cpp $(PAMUL_ROOT)cartographer/new/Kernel.cpp $(PAMUL_ROOT)cartographer/new/IO.cpp $(PAMUL_ROOT)cartographer/new/GraphNode.cpp $(PAMUL_ROOT)cartographer/new/Dijkstra.cpp
ADDSOURCE=
#CXX=$(COMPILER_INSTALL)bin/clang++
CXX=$(COMPILER_INSTALL)bin/g++
CXX_FLAGS=-Wno-c++17-extensions -Wno-deprecated-declarations -g3 -O0
LLD=-fuse-ld=$(COMPILER_INSTALL)bin/ld.lld
LD_PATH=-L$(COMPILER_INSTALL)lib/
A_LINKS=$(SPDLOG_INSTALL)lib/libspdlog.a $(FMT_INSTALL)lib/libfmt.a $(LLVM_INSTALL)lib/libLLVM*
D_LINKS=-lz -lpthread -ltinfo
INCLUDE=-I$(PAMUL_ROOT)cartographer/new/inc/ -I$(PAMUL_ROOT)Util/include/ -I$(LLVM_INSTALL)include/ -I$(LLVM_INSTALL)include/llvm-c/ -I$(SPDLOG_INSTALL)include/ -I$(SPDLOG_INSTALL)../fmt_x64-linux/include/ -I$(NLOHMANNJSON_INSTALL)include/

all : run

$(SOURCE).elf : $(SOURCE).cpp
#	$(CXX) $(LLD) $(CXX_FLAGS) $(INCLUDE) $(LD_PATH) $(D_LINKS) $(A_LINKS) $(ADDSOURCE) $< -o $@
	$(CXX) $(CXX_FLAGS) $(INCLUDE) $(LD_PATH) $(D_LINKS) $(A_LINKS) $(ADDSOURCE) $< -o $@

run : $(SOURCE).elf
	./$< -k $(PAMUL_ROOT)build/Tests/2DConv/kernel.json -b $(PAMUL_ROOT)build/Tests/2DConv/2DConv -o $(PAMUL_ROOT)build/Tests/2DConv/blah.json

.PHONY:

clean :
	rm -rf *.elf
