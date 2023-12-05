CXX_FLAGS := -std=c++20 $(shell pkg-config fmt libcrypto nlohmann_json --cflags) -O0
LD_FLAGS := $(shell pkg-config fmt libcrypto nlohmann_json --libs)
EXE :=

RM := rm


SOURCES := src/ctcli/ctcli.cpp src/analyze.cpp src/config.cpp src/main.cpp src/process.cpp src/file_hash.cpp
HEADERS := src/ctcli/ctcli.h src/ranges/ranges.h src/analyze.h src/cl_options.h src/config.h src/core.h src/process.h src/file_hash.h

ifeq ($(OS),Windows_NT)
	EXE += bin/cppb.exe
	CXX = clang++
	LD = lld
	LD_FLAGS += -fuse-ld=$(LD)
	CXX_FLAGS += -femulated-tls
else
	EXE += bin/cppb
	CXX = clang++-16
	LD = lld-16
	LD_FLAGS += -lpthread -fuse-ld=$(LD)
endif

all: $(EXE)

$(EXE): $(SOURCES) $(HEADERS)
	$(CXX) $(CXX_FLAGS) $(SOURCES) $(LD_FLAGS) -o $@

clean:
	$(RM) $(EXE)
