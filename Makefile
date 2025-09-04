CC := $(shell brew --prefix llvm)/bin/clang++

LLVM_CXXFLAGS := $(shell llvm-config --cxxflags)
LLVM_LDFLAGS  := $(shell llvm-config --ldflags --system-libs --libs core)

CXXFLAGS := -std=c++17 -g -O3 $(LLVM_CXXFLAGS) -Iinclude
SOURCES  := Main.cpp Lexer.cpp Parser.cpp AbstractSyntaxTree.cpp LogErrors.cpp
TARGET   := main

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CXXFLAGS) $(SOURCES) $(LLVM_LDFLAGS) -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
