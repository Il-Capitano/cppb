CXX_FLAGS := -std=c++20 $(shell pkg-config fmt --cflags) -O3
LD_FLAGS := $(shell pkg-config fmt --libs)
EXE :=

RM := rm


SOURCES := src/ctcli/ctcli.cpp src/analyze.cpp src/config.cpp src/main.cpp src/process.cpp
HEADERS := src/ctcli/ctcli.h src/ranges/ranges.h src/analyze.h src/cl_options.h src/config.h src/core.h src/process.h

ifeq ($(OS),Windows_NT)
	EXE += bin/cppb.exe
	CXX = clang++
	LD = lld
	LD_FLAGS += -fuse-ld=$(LD)
	CXX_FLAGS += -femulated-tls
else
	EXE += bin/cppb
	CXX = clang++-13
	LD = lld-13
	LD_FLAGS += -lpthread -fuse-ld=$(LD)
endif

all: $(EXE)

$(EXE): $(SOURCES) $(HEADERS)
	$(CXX) $(CXX_FLAGS) $(SOURCES) $(LD_FLAGS) -o $@

clean:
	$(RM) $(EXE)
