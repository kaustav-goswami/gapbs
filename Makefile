# See LICENSE.txt for license details.

TARGET_ISA=arm64
GEM5_PATH ?= gem5

CXX_FLAGS += -std=c++11 -O3 -Wall -DHOOKS=1 -DEARLYEXIT=1 -I${GEM5_PATH}/include -I${GEM5_PATH}/util/m5/src
LDFLAGS += -L$(GEM5_PATH)/util/m5/build/${TARGET_ISA}/out -lm5
PAR_FLAG = -fopenmp
CC = gcc

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

% : src/%.cc src/*.h $(OBJS)
	$(CXX) $(CXX_FLAGS) -no-pie $< -o $@ $(LDFLAGS)

# Testing
include test/test.mk

# Benchmark Automation
include benchmark/bench.mk


.PHONY: clean
clean:
	rm -f $(SUITE) test/out/*
