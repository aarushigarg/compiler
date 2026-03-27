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
9. [Async, Sync, And Parfor Design](#async-sync-and-parfor-design)
10. [Parfor Benchmark Snapshot](#parfor-benchmark-snapshot)
11. [Debug Information](#debug-information)
12. [Repository Structure](#repository-structure)
13. [What The Current Tests Cover](#what-the-current-tests-cover)

## Project Goal

This project is an ahead-of-time compiler for a small expression-oriented
language with native object-file output and language-level `async` / `sync()` /
`parfor` support.

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
symbols are called directly from `tests/full_coverage.cpp`.

This model is useful for:

- correctness testing
- validating symbol generation and native linkage
- exercising many generated functions in one native harness

### 2. Program-style linking

Any `.cmp` file that defines `def main()` can be compiled into an object file
and linked with `tools/driver.cpp`, which calls the language entrypoint.

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
- parallel loop payload capture compact

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
- `parfor ... in`
- `var ... in`

These features are sufficient to demonstrate:

- expression parsing
- nested scopes
- variable shadowing
- sequential and parallel loop lowering
- conditional control flow in LLVM IR

## Async, Sync, And Parfor Design

The language supports:

- `async functionName(...)`
- `sync()`
- `parfor i = start, end, step in ...`

### Runtime model

The runtime in `runtime.cpp` is a process-wide worker pool built from:

- a shared task queue
- worker threads
- a mutex
- a wakeup condition variable
- a completion condition variable
- a pending-task counter

`sync()` blocks until the pending-task count reaches zero.

### Parfor runtime model

`parfor` uses the same worker pool as `async`, but it waits on its own scoped
completion group instead of the process-wide pending-task counter.

This keeps `parfor` structured:

- the loop does not return until all scheduled chunks complete
- chunk completion is tracked locally per `parfor`
- unrelated async work still uses the global `sync()` barrier

### Parfor lowering strategy

A `parfor` expression is lowered into:

1. evaluation of the start, end, and step expressions
2. by-value capture of visible locals into a heap payload
3. generation of a private chunk wrapper function for the loop body
4. a call to the runtime entrypoint `__compiler_parfor`
5. deallocation of the heap payload after the runtime call returns

The runtime partitions the iteration space into contiguous chunks and schedules
those chunks across the shared worker pool.

### Parfor wrapper design

Each lowered `parfor` site gets a private wrapper with the shape:

```cpp
void task(void *data, std::size_t begin, std::size_t end)
```

The wrapper:

1. reconstructs captured values from the payload
2. computes the current loop value from the chunk-local iteration index
3. binds the loop variable for each iteration
4. executes the source-language body sequentially within that chunk

### Parfor semantics

The current implementation uses these rules:

- `parfor` returns `0.0`
- the step defaults to `1.0`
- the step must be greater than `0`
- the end bound is exclusive
- execution order is not specified
- captures are copied by value into the task payload

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

## Parfor Benchmark Snapshot

The repository includes a simple benchmark in `tests/parfor_benchmark.cmp` and
`tests/parfor_benchmark.cpp` that compares a sequential loop against the
chunked `parfor` implementation on the same workload.

### Workload

The benchmark defines two source-language functions:

- `serialburn(limit)`, which runs a normal `for` loop
- `parallelburn(limit)`, which runs the same loop body with `parfor`

Both functions call the native helper `burn(x)` once per iteration.

`burn(x)` is implemented in C++ and repeatedly evaluates expensive floating
point math operations including `sin`, `cos`, and `sqrt`. The goal is to make
each loop iteration CPU-bound and heavy enough that parallel scheduling has a
meaningful amount of work to distribute.

### Method

The benchmark compares:

- a sequential baseline with `serialburn(limit)`
- a parallel version with `parallelburn(limit)`

Both versions run the same number of iterations and the same per-iteration
native work. That keeps the comparison focused on the difference between
sequential loop execution and chunked `parfor` execution.

The benchmark parameters are:

- `limit=5000`: each benchmarked function executes 5000 loop iterations
- `trials=3`: each function is timed three times and the reported runtime is
  the average of those three measurements

Using multiple trials helps reduce noise from one-off timing variation.

### Result

In one local run, the benchmark reported:

```text
parfor benchmark limit=5000 trials=3
serialburn    49.819 ms
parallelburn  6.788 ms
speedup       7.34x
```

### Interpretation

This result shows that the current `parfor` implementation is doing more than
issuing many tiny tasks. The runtime groups iterations into chunks, schedules
those chunks over the shared worker pool, and waits for the loop to complete as
one structured parallel operation.

The exact numbers are machine-dependent, but this benchmark is useful for
showing the intended systems behavior:

- a sequential baseline using the same source-language workload
- a parallel lowering path that reuses the runtime worker pool
- a measurable speedup from chunked loop execution

This benchmark is intended as a compact demonstration, not a full performance
study. It shows that the `parfor` design can improve wall-clock time on a
CPU-bound workload, but it does not attempt to characterize all workloads,
machines, or scheduler configurations.

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
- `tests/parfor_coverage.cmp`: parallel-loop coverage input
- `tests/parfor_test_driver.cpp`: parallel-loop correctness harness
- `tests/parfor_benchmark.cmp`: benchmark input
- `tests/parfor_benchmark.cpp`: benchmark driver
- `tests/full_coverage.cmp`: feature-coverage input
- `tests/full_coverage.cpp`: library-style correctness harness
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

`tests/parfor_coverage.cmp` exercises:

- basic `parfor` ranges
- default and explicit step handling
- by-value capture of outer locals
- nested `parfor`
- empty ranges

`tests/parfor_benchmark.cmp` and `tests/parfor_benchmark.cpp` provide a simple
sequential-versus-parallel benchmark for the loop runtime.
