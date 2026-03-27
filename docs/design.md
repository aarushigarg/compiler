# Design Notes

## Table Of Contents

1. [Project Goal](#project-goal)
2. [Compile Pipeline](#compile-pipeline)
3. [Execution Models](#execution-models)
4. [Entrypoint Design](#entrypoint-design)
5. [Top-level Source Design](#top-level-source-design)
6. [Type Model](#type-model)
7. [Operator Design](#operator-design)
8. [Control-flow And Scope Support](#control-flow-and-scope-support)
9. [Async And Sync Design](#async-and-sync-design)
10. [Debug Information](#debug-information)
11. [Repository Structure](#repository-structure)
12. [What The Current Tests Cover](#what-the-current-tests-cover)

## Project Goal

This project is an ahead-of-time compiler for a small expression-oriented
language with native object-file output and language-level `async` / `sync()`
support.

The compiler is responsible for:

1. reading a source file
2. building an AST
3. lowering that AST to LLVM IR
4. emitting a native object file

Execution is handled by native C++ code linked with the generated object and
the runtime in `runtime.cpp`.

## Compile Pipeline

The compiler pipeline is organized around these stages:

1. lexing in `Lexer.*`
2. recursive-descent parsing in `Parser.*`
3. AST construction in `AbstractSyntaxTree.*`
4. LLVM IR generation in `AbstractSyntaxTree.cpp`
5. object-file emission in `Main.cpp`

This keeps the front end, code generation, and runtime support separated while
still keeping the project small enough to follow end to end.

## Execution Models

The generated object files are used in two ways.

### 1. Library-style linking

`tests/full_coverage.cmp` is compiled into an object file whose generated
symbols are called directly from `tests/test_driver.cpp`.

This model is useful for:

- correctness testing
- validating symbol generation and native linkage
- exercising many generated functions in one native harness

### 2. Program-style linking

`tests/program.cmp` is compiled into an object file and linked with
`tools/driver.cpp`, which calls the language entrypoint.

This model is useful for:

- showing a conventional compile-link-run path
- demonstrating a standard program entrypoint
- running a `.cmp` file as a program rather than as a library

## Entrypoint Design

The language-level entrypoint is written as:

```text
def main()
  ...
```

When present, that function is lowered to the native symbol:

```text
__program_main
```

This keeps the language entrypoint separate from the C++ executable entrypoint:

```cpp
int main()
```

The standard driver in `tools/driver.cpp` calls `__program_main`.

The compiler also reports whether a valid zero-argument `main` was found after
successful compilation.

## Top-level Source Design

Top-level source is restricted to:

- `def`
- `extern`

Bare top-level expressions are not part of the language surface for this
project. That keeps the source structure aligned with object-file generation and
native linking.

Expressions are expected to live inside function bodies.

## Type Model

All source-language values are currently compiled as `double`.

This choice keeps:

- the parser simple
- the code generation path compact
- the async payload representation uniform
- the runtime ABI straightforward

## Operator Design

The language supports:

- built-in binary operators `<`, `+`, `-`, `*`
- user-defined unary operators
- user-defined binary operators with explicit or default precedence

Operator precedence is handled in the parser with a precedence table and
right-hand-side parsing for binary expressions.

## Control-flow And Scope Support

The language includes:

- function definitions and calls
- `if ... then ... else ...`
- `for ... in`
- `var ... in`

These features are sufficient to demonstrate:

- expression parsing
- nested scopes
- variable shadowing
- loop lowering
- conditional control flow in LLVM IR

## Async And Sync Design

The language supports:

- `async functionName(...)`
- `sync()`

### Runtime model

The runtime in `runtime.cpp` is a process-wide worker pool built from:

- a shared task queue
- worker threads
- a mutex
- a wakeup condition variable
- a completion condition variable
- a pending-task counter

`sync()` blocks until the pending-task count reaches zero.

### Lowering strategy

An async call site is lowered into:

1. evaluation of the async argument expressions
2. heap allocation for the argument payload
3. storage of those argument values into the payload
4. generation of a private wrapper function for the call site
5. a call to the runtime entrypoint `__compiler_async_call`

### Wrapper design

Runtime tasks use a generic function shape:

```cpp
void task(void *data)
```

Source-language functions do not share one fixed signature, so each async site
gets a generated wrapper. The wrapper:

1. reads the payload
2. reconstructs the original argument list
3. calls the real callee
4. frees the payload

### Payload storage choice

Async argument data is stored on the heap so the payload remains valid after the
caller continues execution or returns.

## Debug Information

The compiler emits LLVM debug metadata into the generated module. Source
locations are tracked through `SourceLocation.h` and used during codegen to
annotate emitted IR with line and column information.

## Repository Structure

The repository is organized around these roles:

- `Lexer.*`: tokenization
- `Parser.*`: parsing
- `AbstractSyntaxTree.*`: AST and code generation
- `Main.cpp`: compile pipeline and object emission
- `runtime.cpp`: runtime support for async and sync
- `tests/full_coverage.cmp`: feature-coverage input
- `tests/test_driver.cpp`: library-style correctness harness
- `tests/program.cmp`: program-style input
- `tools/driver.cpp`: standard native program driver
- `docs/usage.md`: language and workflow reference
- `docs/design.md`: design and implementation record

## What The Current Tests Cover

`tests/full_coverage.cmp` exercises:

- ordinary function definitions and calls
- built-in operators
- custom unary and binary operators
- conditionals
- loops
- local bindings and shadowing
- extern declarations
- `async`
- `sync()`

`tests/program.cmp` exercises the standard program-entry flow through the
general driver.
