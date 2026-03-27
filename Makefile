CC := $(shell brew --prefix llvm)/bin/clang++

LLVM_CXXFLAGS := $(shell llvm-config --cxxflags)
LLVM_LDFLAGS  := $(shell llvm-config --ldflags --system-libs --libs all)

CXXFLAGS := -std=c++17 -g -O3 $(LLVM_CXXFLAGS) -Iinclude
TEST_CXXFLAGS := -std=c++17 -g -O3
SOURCES  := Main.cpp Lexer.cpp Parser.cpp AbstractSyntaxTree.cpp LogErrors.cpp Debug.cpp
TARGET   := main
TEST_SOURCE := tests/full_coverage.cmp
TEST_OBJECT := tests/full_coverage.o
TEST_DRIVER := tests/runtime_driver.cpp
TEST_RUNNER := runtime_tests

.PHONY: all clean run run-dev test test-compile test-correctness

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CXXFLAGS) $(SOURCES) $(LLVM_LDFLAGS) -o $@

run: $(TARGET)
	./$(TARGET)

run-dev: $(TARGET)
	./$(TARGET) --dev

test: test-compile test-correctness

test-compile: $(TARGET)
	./$(TARGET) --file $(TEST_SOURCE)

$(TEST_RUNNER): $(TEST_OBJECT) $(TEST_DRIVER)
	$(CC) $(TEST_CXXFLAGS) $(TEST_DRIVER) $(TEST_OBJECT) -lm -o $(TEST_RUNNER)

test-correctness: $(TARGET) $(TEST_RUNNER)
	./$(TARGET) --file $(TEST_SOURCE)
	./$(TEST_RUNNER)

clean:
	rm -f $(TARGET) $(TEST_RUNNER) *.o
