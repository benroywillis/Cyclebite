#LLVM_INSTALL=/usr/lib/llvm-9/
LLVM_INSTALL=/mnt/heorot-10/bwilli46/Installs/LLVM9/release/
PAMUL_ROOT=../../
SPDLOG_INSTALL=/mnt/nobackup-11/bwilli46/vcpkg/packages/spdlog_x64-linux/
FMT_INSTALL=/mnt/nobackup-11/bwilli46/vcpkg/packages/fmt_x64-linux/
NLOHMANNJSON_INSTALL=/mnt/nobackup-11/bwilli46/vcpkg/packages/nlohmann-json_x64-linux/

SOURCE=Transforms_tb
SOURCE_FUNCTION=FunctionInline_tb
SOURCE_SEGMENTATION=Segmentation_tb
CXX=$(LLVM_INSTALL)/bin/clang++
CXX_FLAGS=-Wno-c++17-extensions -Wno-deprecated-declarations -g3 -O0
LLD=-fuse-ld=$(LLVM_INSTALL)bin/ld.lld
A_LINKS=$(SPDLOG_INSTALL)lib/libspdlog.a $(FMT_INSTALL)lib/libfmt.a $(LLVM_INSTALL)lib/*.a
D_LINKS=-lz $(PAMUL_ROOT)build/lib/libGraph.so

INCLUDE=-I$(PAMUL_ROOT)Graph/inc/ -I$(PAMUL_ROOT)AtlasUtil/include/ -I$(LLVM_INSTALL)include/ -I$(LLVM_INSTALL)include/llvm-c/ -I$(SPDLOG_INSTALL)include/ -I$(SPDLOG_INSTALL)../fmt_x64-linux/include/ -I$(NLOHMANNJSON_INSTALL)include/

all : run

$(SOURCE).elf : $(SOURCE).cpp
	$(CXX) $(LLD) $(CXX_FLAGS) $(INCLUDE) $(D_LINKS) $(A_LINKS) $(ADDSOURCE) $< -o $@

$(SOURCE_FUNCTION).elf : $(SOURCE_FUNCTION).cpp
	$(CXX) $(LLD) $(CXX_FLAGS) $(INCLUDE) $(D_LINKS) $(A_LINKS) $(ADDSOURCE) $< -o $@

$(SOURCE_SEGMENTATION).elf : $(SOURCE_SEGMENTATION).cpp
	$(CXX) $(LLD) $(CXX_FLAGS) $(INCLUDE) $(D_LINKS) $(A_LINKS) $(ADDSOURCE) $< -o $@

run_transform : $(SOURCE).elf
	LD_LIBRARY_PATH=$(PAMUL_ROOT)build/lib/ ./$(SOURCE).elf

run_function : $(SOURCE_FUNCTION).elf
	LD_LIBRARY_PATH=$(PAMUL_ROOT)build/lib/ ./$(SOURCE_FUNCTION).elf

run_segmentation : $(SOURCE_SEGMENTATION).elf
	LD_LIBRARY_PATH=$(PAMUL_ROOT)build/lib/ ./$(SOURCE_SEGMENTATION).elf

run : $(SOURCE).elf $(SOURCE_FUNCTION).elf $(SOURCE_SEGMENTATION).elf
	LD_LIBRARY_PATH=$(PAMUL_ROOT)build/lib/ ./$(SOURCE).elf
	LD_LIBRARY_PATH=$(PAMUL_ROOT)build/lib/ ./$(SOURCE_FUNCTION).elf

.PHONY:

clean :
	rm -rf *.elf