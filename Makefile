CXX := g++
CXXFLAGS := \
	-std=c++23 \
	-Wall -Wextra -Wpedantic -Werror \
	-Wshadow -Wconversion -Wsign-conversion -Wnull-dereference \
	-Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough \
	-Wold-style-cast -Woverloaded-virtual -Wcast-align \
	-Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wuseless-cast \
	-fno-omit-frame-pointer -g3 \
	-I include

LDFLAGS :=

SANITIZERS := -fsanitize=address -fsanitize=undefined -fsanitize=leak

SRC_DIR := src
BUILD_DIR := build
TEST_DIR := tests
FUZZ_DIR := fuzz

LIB_SRCS := $(SRC_DIR)/order.cpp $(SRC_DIR)/level_info.cpp $(SRC_DIR)/trade.cpp \
            $(SRC_DIR)/order_book.cpp $(SRC_DIR)/matching_engine.cpp \
            $(SRC_DIR)/market_data.cpp $(SRC_DIR)/wal.cpp $(SRC_DIR)/terminal.cpp
LIB_OBJS := $(LIB_SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
LIB := $(BUILD_DIR)/liborderbook.a

CLI_SRC := $(SRC_DIR)/main.cpp
CLI_OBJ := $(BUILD_DIR)/main.o
CLI_OUT := $(BUILD_DIR)/orderbook

TEST_SRCS := $(TEST_DIR)/order_book_tests.cpp $(TEST_DIR)/matching_engine_tests.cpp $(TEST_DIR)/event_tests.cpp $(TEST_DIR)/market_data_tests.cpp $(TEST_DIR)/concurrent_tests.cpp
TEST_OBJS := $(TEST_SRCS:$(TEST_DIR)/%.cpp=$(BUILD_DIR)/%.o)
TEST_OUT := $(BUILD_DIR)/orderbook_tests

FUZZ_SRC := $(FUZZ_DIR)/order_book_fuzz.cpp
FUZZ_OBJ := $(BUILD_DIR)/order_book_fuzz.o
FUZZ_OUT := $(BUILD_DIR)/orderbook_fuzz

.PHONY: all build run test clean release

all: build

build: $(CLI_OUT)

test: $(TEST_OUT)
	./$(TEST_OUT)

run: build
	./$(CLI_OUT)

$(LIB): $(LIB_OBJS)
	@mkdir -p $(BUILD_DIR)
	ar rcs $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SANITIZERS) -c $< -o $@

$(CLI_OBJ): $(CLI_SRC)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SANITIZERS) -c $< -o $@

$(CLI_OUT): $(LIB) $(CLI_OBJ)
	$(CXX) $(CXXFLAGS) $(SANITIZERS) $(CLI_OBJ) -L$(BUILD_DIR) -lorderbook -o $@

$(BUILD_DIR)/%.o: $(TEST_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SANITIZERS) -c $< -o $@

$(TEST_OUT): $(LIB) $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(SANITIZERS) $(TEST_OBJS) -L$(BUILD_DIR) -lorderbook \
		-lgtest -lgtest_main -lpthread -o $@

$(BUILD_DIR)/%.o: $(FUZZ_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DSTANDALONE_FUZZ=1 $(SANITIZERS) -c $< -o $@

$(FUZZ_OUT): $(LIB) $(FUZZ_OBJ)
	$(CXX) $(CXXFLAGS) $(SANITIZERS) $(FUZZ_OBJ) -L$(BUILD_DIR) -lorderbook -o $@

release:
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(SRC_DIR)/*.cpp -I include \
		-std=c++23 -O3 -march=native -flto -DNDEBUG \
		$(CLI_SRC) -o $(CLI_OUT)

release-test:
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(LIB_SRCS) $(TEST_SRCS) -I include \
		-std=c++23 -O3 -march=native -flto -DNDEBUG \
		-lgtest -lgtest_main -lpthread -o $(TEST_OUT)
	./$(TEST_OUT)

clean:
	rm -rf $(BUILD_DIR)
