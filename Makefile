CC := $(shell brew --prefix llvm)/bin/clang++

LLVM_CXXFLAGS := $(shell llvm-config --cxxflags)
LLVM_LDFLAGS  := $(shell llvm-config --ldflags --system-libs --libs all)

CXXFLAGS := -std=c++17 -g -O3 $(LLVM_CXXFLAGS) -Iinclude
TEST_CXXFLAGS := -std=c++17 -g -O3
SOURCES  := Main.cpp Lexer.cpp Parser.cpp AbstractSyntaxTree.cpp LogErrors.cpp
TARGET   := main
TEST_SOURCE := tests/full_coverage.cmp
TEST_OBJECT := tests/full_coverage.o
TEST_DRIVER := tests/runtime_driver.cpp
TEST_RUNNER := runtime_tests
RUNTIME_SOURCE := runtime.cpp
RUNTIME_OBJECT := runtime.o

.PHONY: all clean run test test-compile test-correctness

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CXXFLAGS) $(SOURCES) $(LLVM_LDFLAGS) -o $@

run: $(TARGET)
	./$(TARGET)

test: test-compile test-correctness

test-compile: $(TEST_OBJECT)

$(TEST_OBJECT): $(TARGET) $(TEST_SOURCE)
	./$(TARGET) --file $(TEST_SOURCE)

$(TEST_RUNNER): $(TEST_OBJECT) $(TEST_DRIVER) $(RUNTIME_OBJECT)
	$(CC) $(TEST_CXXFLAGS) $(TEST_DRIVER) $(TEST_OBJECT) $(RUNTIME_OBJECT) -lm -o $(TEST_RUNNER)

test-correctness: $(TARGET) $(TEST_RUNNER)
	./$(TARGET) --file $(TEST_SOURCE)
	./$(TEST_RUNNER)

$(RUNTIME_OBJECT): $(RUNTIME_SOURCE)
	$(CC) $(TEST_CXXFLAGS) -c $(RUNTIME_SOURCE) -o $(RUNTIME_OBJECT)

clean:
	rm -f $(TARGET) $(TEST_RUNNER) *.o
