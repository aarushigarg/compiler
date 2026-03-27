CC := $(shell brew --prefix llvm)/bin/clang++
LLVM_CXXFLAGS := $(shell llvm-config --cxxflags)
LLVM_LDFLAGS := $(shell llvm-config --ldflags --system-libs --libs all)

CXXFLAGS := -std=c++17 -g -O3 $(LLVM_CXXFLAGS) -Iinclude
TEST_CXXFLAGS := -std=c++17 -g -O3
COMPILER_SOURCES := Main.cpp Lexer.cpp Parser.cpp AbstractSyntaxTree.cpp LogErrors.cpp
TARGET := main
RUNTIME_OBJECT := runtime.o
PROGRAM ?=
PROGRAM_OBJECT := $(patsubst %.cmp,%.o,$(PROGRAM))

.PHONY: all clean run test test-driver

all: $(TARGET)

$(TARGET): $(COMPILER_SOURCES)
	$(CC) $(CXXFLAGS) $(COMPILER_SOURCES) $(LLVM_LDFLAGS) -o $@

run: $(TARGET) $(RUNTIME_OBJECT)
	@if [ -z "$(PROGRAM)" ]; then echo "Usage: make run PROGRAM=path/to/file.cmp"; exit 1; fi
	./$(TARGET) $(PROGRAM)
	$(CC) $(TEST_CXXFLAGS) tools/driver.cpp $(PROGRAM_OBJECT) $(RUNTIME_OBJECT) -lm -o program_runner
	./program_runner

test: $(TARGET) $(RUNTIME_OBJECT)
	./$(TARGET) tests/full_coverage.cmp
	$(CC) $(TEST_CXXFLAGS) tests/test_driver.cpp tests/full_coverage.o $(RUNTIME_OBJECT) -lm -o runtime_tests
	./runtime_tests

test-driver: $(TARGET) $(RUNTIME_OBJECT)
	./$(TARGET) tests/program.cmp
	$(CC) $(TEST_CXXFLAGS) tools/driver.cpp tests/program.o $(RUNTIME_OBJECT) -lm -o program_runner
	./program_runner

$(RUNTIME_OBJECT): runtime.cpp
	$(CC) $(TEST_CXXFLAGS) -c runtime.cpp -o $(RUNTIME_OBJECT)

clean:
	rm -f $(TARGET) runtime_tests program_runner *.o tests/*.o
