# See LICENSE.txt for license details.

CROSS_COMPILE = aarch64-linux-gnu-
CXX_VERSION = -10
CXX = ${CROSS_COMPILE}g++${CXX_VERSION}
CXX_FLAGS += -std=c++11 -O3 -Wall -DM5OP_ADDR=0x10010000
PAR_FLAG = -fopenmp
C_VERSION = -10
CC = ${CROSS_COMPILE}gcc${C_VERSION}

CCOMPILE = $(CC)  -c $(C_INC) $(CFLAGS)
COMMON = src
GEM5DIR = gem5

ifneq (,$(findstring icpc,$(CXX)))
	PAR_FLAG = -openmp
endif

ifneq (,$(findstring sunCC,$(CXX)))
	CXX_FLAGS = -std=c++11 -xO3 -m64 -xtarget=native
	PAR_FLAG = -xopenmp
endif

ifneq ($(SERIAL), 1)
	CXX_FLAGS += $(PAR_FLAG)
endif

KERNELS = bc bfs cc cc_sv pr sssp tc
SUITE = $(KERNELS) converter

.PHONY: all
all: $(SUITE)


${COMMON}/hooks.o: ${COMMON}/hooks.c
	cd ${COMMON}; ${CCOMPILE} hooks.c -Wno-implicit-function-declaration -I ${GEM5DIR}/include/
${COMMON}/m5op_arm64.o: ${COMMON}/m5op_arm64.S
	cd ${COMMON}; gcc -O2 m5op_arm64.S -DM5OP_ADDR=0x10010000 -I ${GEM5DIR}/include/ -o ../$@ -c 
${COMMON}/m5_mmap.o: ${COMMON}/m5_mmap.c
	cd ${COMMON}; ${CCOMPILE} ${COMMON}/m5_mmap.c -Wno-implicit-function-declaration -I ${GEM5DIR}/include/
OBJS =
ifeq (${HOOKS}, 1)
        OBJS += ${COMMON}/m5op_arm64.o
endif

ifeq (${HOOKS}, 1)
       CXX_FLAGS += -DHOOKS
endif

% : src/%.cc src/*.h $(OBJS)
	$(CXX) $(CXX_FLAGS) $< -o $@ ${COMMON}/m5_mmap.c  $(OBJS) -no-pie

# Testing
include test/test.mk

# Benchmark Automation
include benchmark/bench.mk


.PHONY: clean
clean:
	rm -f $(SUITE) test/out/*
	rm -f src/hooks.o

