# Usage Guide

## Overview

This compiler reads a `.cmp` source file and emits a native object file.
Execution is performed by linking that object with native C++ code and the
runtime in `runtime.cpp`.

There are two supported usage patterns in this repository:

- library-style linking through `tests/full_coverage.cpp`
- program-style linking through `tools/driver.cpp`
- benchmark-style linking through `tests/parfor_benchmark.cpp`

## Requirements

- Homebrew LLVM installed
- `clang++` from that LLVM toolchain
- `llvm-config` available on `PATH`

Install LLVM on macOS:

```sh
brew install llvm
```

## Build

Build the compiler:

```sh
make
```

This produces:

```text
main
```

## Compile

Compile any source file directly:

```sh
./main path/to/file.cmp
```

This emits:

```text
path/to/file.o
```

After a successful compile, the compiler also reports:

- `Entrypoint: main found`
- `Entrypoint: no main function`

## Supported Workflows

### Library-style test flow

Use this when the compiled `.cmp` file acts like a library of exported
functions.

Run:

```sh
make test
```

This:

1. compiles `tests/full_coverage.cmp`
2. links the result with `tests/full_coverage.cpp` and `runtime.cpp`
3. executes native correctness checks
4. compiles and runs the dedicated `parfor` correctness harness

Run only the `parfor` correctness checks:

```sh
make test-parfor
```

### Program-style driver flow

Use this when the `.cmp` file defines a program entrypoint:

```text
def main()
  ...
```

Run the built-in program-style test:

```sh
make run PROGRAM=path/to/file.cmp
```

This:

1. compiles the `.cmp` file to an object file
2. links it with `tools/driver.cpp` and `runtime.cpp`
3. executes the generated program entrypoint

### Benchmark flow

Run the parallel loop benchmark:

```sh
make benchmark-parfor
```

This:

1. compiles `tests/parfor_benchmark.cmp`
2. links it with `tests/parfor_benchmark.cpp` and `runtime.cpp`
3. reports sequential versus parallel runtime for the benchmark workload

## Source Structure Rules

### Top-level forms

Only these forms are allowed at top level:

- `def`
- `extern`

Bare top-level expressions are rejected.

### Entry point

`main` is optional for compilation. A file without `main` can still be compiled
and linked through a custom native harness.

When a file defines:

```text
def main()
  ...
```

the compiler lowers it to:

```text
__program_main
```

The standard driver in `tools/driver.cpp` calls that symbol.

## Language Reference

### Values

All values are currently compiled as `double`.

### Comments

Comments begin with `#` and continue to the end of the line.

### Functions

Define a function:

```text
def add(x y)
  x + y
```

Declare an external function:

```text
extern sin(x)
```

Call a function:

```text
add(1, 2)
```

### Operators

Built-in binary operators:

- `<`
- `+`
- `-`
- `*`

User-defined unary operator example:

```text
def unary!(x)
  0 - x
```

User-defined binary operator example:

```text
def binary% 60 (x y)
  x * y + x
```

If no custom binary precedence is given, the default precedence is `30`.

### Conditionals

```text
if x < y then x else y
```

### Loops

```text
for i = 1, i < 10, 1 in
  i * 2
```

The step expression is optional:

```text
for i = 1, i < 10 in
  i
```

Parallel loop example:

```text
parfor i = 0, 8, 1 in
  printd(i)
```

Behavior:

- `parfor` evaluates the start, end, and step expressions once
- the end bound is exclusive
- the step defaults to `1.0`
- the step must be greater than `0`
- `parfor` returns `0.0`
- iteration order is not specified

### Local bindings

```text
var a = 1, b = 2 in
  a + b
```

Omitted initializers default to `0.0`:

```text
var x, y = 3 in
  x + y
```

### Async and sync

Async scheduling:

```text
async printd(99)
```

Barrier synchronization:

```text
sync()
```

Combined use:

```text
var ignored = async printd(99) in
  sync() + ignored
```

Behavior:

- `async f(args...)` schedules the call on the runtime worker pool
- `sync()` blocks until outstanding async work completes
- `async` currently evaluates to `0.0`
- `async` currently requires a direct function name: `async functionName(...)`

## Grammar Summary

### Top level

```text
top ::= definition
     | external
```

### Expressions

```text
expression ::= unary binoprhs

primary ::= identifierexpr
          | numberexpr
          | parenexpr
          | ifexpr
          | forexpr
          | varexpr
          | asyncexpr
          | syncexpr
```

### Identifiers and calls

```text
identifierexpr ::= identifier
                 | identifier '(' expression (',' expression)* ')'
```

### Control flow

```text
ifexpr ::= 'if' expression 'then' expression 'else' expression

forexpr ::= 'for' identifier '=' expression ','
            expression
            (',' expression)?
            'in' expression
```

### Variables

```text
varexpr ::= 'var' identifier ('=' expression)?
            (',' identifier ('=' expression)?)* 'in' expression
```

### Async and sync

```text
asyncexpr ::= 'async' identifier '(' expression (',' expression)* ')'
syncexpr  ::= 'sync' '(' ')'
```

### Prototypes

```text
prototype ::= identifier '(' identifier* ')'
            | 'unary' ASCII '(' identifier ')'
            | 'binary' ASCII number? '(' identifier identifier ')'
```

## Useful Files

- `tests/full_coverage.cmp`
- `tests/full_coverage.cpp`
- `tools/driver.cpp`
- `runtime.cpp`

## Commands

Build:

```sh
make
```

Run library-style tests:

```sh
make test
```

Run any program file:

```sh
make run PROGRAM=path/to/file.cmp
```

Clean generated artifacts:

```sh
make clean
```
