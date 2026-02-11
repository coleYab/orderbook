CXX := g++
CXXFLAGS := \
	-std=c++23 \
	-Wall \
	-Wextra \
	-Wpedantic \
	-Werror \
	-Wshadow \
	-Wconversion \
	-Wsign-conversion \
	-Wnull-dereference \
	-Wdouble-promotion \
	-Wformat=2 \
	-Wimplicit-fallthrough \
	-Wold-style-cast \
	-Woverloaded-virtual \
	-Wnon-virtual-dtor \
	-Wcast-align \
	-Wduplicated-cond \
	-Wduplicated-branches \
	-Wlogical-op \
	-Wuseless-cast \
	-fno-omit-frame-pointer \
	-g3

SANITIZERS := \
	-fsanitize=address \
	-fsanitize=undefined \
	-fsanitize=leak

LDFLAGS := $(SANITIZERS)

SRC := main.cpp
OUT := build/orderbook

.PHONY: build run clean release

build:
	mkdir -p build
	$(CXX) $(CXXFLAGS) $(SANITIZERS) $(SRC) -o $(OUT)

run: build
	./$(OUT)

release:
	mkdir -p build
	$(CXX) $(SRC) \
		-std=c++23 \
		-O3 \
		-march=native \
		-flto \
		-DNDEBUG \
		-o $(OUT)

clean:
	rm -rf build