# Compiler

An ahead-of-time compiler for a small expression-oriented language built with
LLVM.

The compiler parses `.cmp` source files, lowers them to LLVM IR, and emits
native object files. Generated code links against a C++ runtime that provides
language-level `async` and `sync()` support through a worker-pool execution
model.

## What It Does

The project covers the full path from source code to native object output:

- lexing and recursive-descent parsing for a custom language
- AST construction and LLVM IR generation
- native object-file emission
- language features such as functions, conditionals, loops, scoped locals, and
  custom operators
- async task scheduling and barrier synchronization
- native integration through both a general program driver and a direct test
  harness

## Architecture

The project has three main pieces.

### Compiler front end and code generation

The compiler reads a source file, builds an AST, and lowers that AST to LLVM
IR. The output is a native object file that can be linked like any other
compiled object.

### Async runtime

The runtime in [runtime.cpp](runtime.cpp) provides the execution support for
`async` and `sync()`. Async call sites are lowered into runtime task
submissions, and `sync()` acts as a barrier over outstanding work.

### Native execution paths

The generated object files are used in two ways:

- [tests/test_driver.cpp](tests/test_driver.cpp) links against generated functions directly for correctness testing
- [tools/driver.cpp](tools/driver.cpp) runs a compiled language `main` through the lowered symbol `__program_main`

## Language

The language currently supports:

- `def` function definitions
- `extern` declarations
- function calls
- built-in operators `<`, `+`, `-`, `*`
- user-defined unary and binary operators
- `if ... then ... else ...`
- `for ... in`
- `var ... in`
- `async functionName(...)`
- `sync()`
- `#` line comments

All values are currently compiled as `double`.

Top-level source is restricted to `def` and `extern`. Files intended for the
standard driver define:

```text
def main()
  ...
```

The compiler reports whether a valid zero-argument `main` was found after a
successful compile.

## Running It

The main workflows are:

```sh
make
make test
make test-driver
make run PROGRAM=tests/program.cmp
```

## More Detail

For the design and implementation notes, see
[docs/design.md](docs/design.md).

The full command guide and language reference are in
[docs/usage.md](docs/usage.md).
