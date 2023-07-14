LLVM_INSTALL=/mnt/heorot-10/bwilli46/LLVM9/install-release/
#LLVM_INSTALL=/mnt/heorot-10/bwilli46/LLVM12/install-release/
#TRACEATLAS_ROOT=/home/bwilli46/Install/Cyclebite_dev/
TRACEATLAS_ROOT=/home/bwilli46/Cyclebite/build/
#TRACEATLAS_ROOT=/home/bwilli46/Cyclebite/build_RelWithDebInfo/
#TRACEATLAS_ROOT=/home/bwilli46/Cyclebite/build_Release/
TRACEATLAS_HC_ROOT=/home/bwilli46/Install/Cyclebite_hotCode/

# Halide install
#HALIDE_INSTALL_PREFIX=/mnt/heorot-10/bwilli46/Halide-install-release/
# Halide 10 install
HALIDE_INSTALL_PREFIX=/mnt/heorot-10/bwilli46/Halide10-install-release/
#HALIDE_INSTALL_PREFIX=/mnt/heorot-10/bwilli46/Halide10-install-debug/
#HALIDE_COMPILE_ARGS=-std=c++11 -fno-rtti
HALIDE_COMPILE_ARGS=-fno-rtti
HALIDE_D_LINKS=-lpthread -ldl -lpng -ljpeg
HALIDE_INCLUDE=\
-I$(HALIDE_INSTALL_PREFIX)include/\
-I$(HALIDE_INSTALL_PREFIX)share/tools

# some library installs
DASH_ROOT=/mnt/heorot-10/bwilli46/dash-archives/debug/
GSL_ROOT=$(DASH_ROOT)gsl/
FFTW_ROOT=$(DASH_ROOT)fftw/
OPENCV_ROOT=$(DASH_ROOT)opencv/

