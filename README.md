# Compiler

This project is a small LLVM-based compiler for an expression-oriented language. It parses source code into an AST, lowers it to LLVM IR, and emits an object file. The generated code can be linked against a small C++ runtime that provides asynchronous task execution through a general worker-pool design.

## Requirements

- Homebrew LLVM installed
- `clang++` from that LLVM toolchain
- `llvm-config` available on `PATH`

Install the dependency on macOS with:

```sh
brew install llvm
```

## Build

Build the compiler:

```sh
make
```

The build produces an executable named `main`.

## Input Modes

The compiler supports two input modes.

### Standard input mode

This is the interactive mode. It reads from `stdin` and prints `ready>` prompts:

```sh
make run
```

You can also select it explicitly:

```sh
./main --stdin
```

### File mode

This reads directly from a source file and runs without interactive prompts:

```sh
./main --file tests/full_coverage.cmp
```

In file mode, the output object uses the input filename with an `.o` extension. For example, `tests/full_coverage.cmp` becomes `tests/full_coverage.o`.

## Language Features

The compiler currently supports:

- Numeric literals as `double`
- Variables and variable references
- Function definitions with `def`
- External declarations with `extern`
- Function calls
- Top-level expressions
- Built-in binary operators: `<`, `+`, `-`, `*`
- Parenthesized expressions
- `if ... then ... else ...`
- `for ... in` loops
- `var ... in` local bindings
- `async` task scheduling expressions
- `sync()` barrier expressions
- User-defined unary operators
- User-defined binary operators with custom precedence
- `#` line comments
- Debug metadata emission into the generated object file

## Quick Syntax Guide

If you just want the most common source forms, start here:

```text
def add(x y) x + y
extern sin(x)
add(1, 2)
if x < y then x else y
for i = 1, i < 10, 1 in i * 2
var a = 1, b = 2 in a + b
async printd(42)
sync()
def unary!(x) 0 - x
def binary% 50 (x y) x - y
```

Quick reminders:

- `for i = 1, i < 10 in i` is also valid; the step expression is optional
- `var x in ...` is valid; omitted initializers default to `0.0`
- custom binary operators default to precedence `30` if no number is provided
- `async` currently only supports direct named function calls

## Grammar Overview

### How to read the grammar

- `::=` means "is defined as"
- `|` means "or"
- `(...)` groups parts together
- `?` means "optional"
- `*` means "zero or more"

### Top-level input

At the top level, the compiler accepts a function definition, an extern declaration, or a normal expression.

```text
top ::= definition
     | external
     | expression
```

### Expressions

An expression starts from a unary expression and may continue with binary operators according to precedence rules.

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

Examples:

```text
42
x
(a + b) * 2
async printd(42)
sync()
```

### Identifiers and function calls

An identifier can be either:

- a variable reference like `x`
- a function call like `add(1, 2)`

```text
identifierexpr ::= identifier
                 | identifier '(' expression (',' expression)* ')'
```

### Control flow

The language supports `if` expressions and `for` expressions. Both produce values like any other expression.

```text
ifexpr ::= 'if' expression 'then' expression 'else' expression

forexpr ::= 'for' identifier '=' expression ','
            expression
            (',' expression)?
            'in' expression
```

### Local variables

A `var` expression introduces one or more local bindings, then evaluates a body expression after `in`.

```text
varexpr ::= 'var' identifier ('=' expression)?
            (',' identifier ('=' expression)?)* 'in' expression
```

Examples:

```text
var a = 1 in a + 2
var a = 1, b = 2 in a + b
var x, y = 3 in x + y
var ignored = async printd(99) in
  sync() + ignored
```

### Async and sync

The current task-parallel syntax is:

```text
asyncexpr ::= 'async' identifier '(' expression (',' expression)* ')'
syncexpr  ::= 'sync' '(' ')'
```

Examples:

```text
async printd(99)
sync()

var ignored = async printd(99) in
  sync() + ignored
```

Meaning:

- `async f(args...)` schedules `f(args...)` to run on the runtime worker pool
- `sync()` blocks until all outstanding async work has finished

### Function signatures

A prototype describes the name and parameters of a function or operator.

```text
prototype ::= identifier '(' identifier* ')'
            | 'unary' ASCII '(' identifier ')'
            | 'binary' ASCII number? '(' identifier identifier ')'
```

## Async/Sync Runtime

This project includes a native runtime in [`runtime.cpp`](/runtime.cpp). The compiler lowers high-level `async` and `sync()` expressions into calls to runtime entry points implemented in C++.

### What `async` does

`async` is a scheduling operation, not a normal function call. For example:

```text
async printd(99)
```

does not call `printd(99)` immediately on the current thread. Instead, the compiler lowers it to a runtime helper that:

1. evaluates the argument expressions
2. stores those values in a heap payload
3. generates a wrapper function for that async site
4. passes the wrapper and payload pointer to the runtime
5. returns immediately to the caller

In the current language implementation, `async ...` returns `0.0`. The async task's useful effect is side effects or eventual completion, not returning a value back into the source language.

### What `sync()` does

`sync()` is a barrier. It blocks until the runtime's count of outstanding tasks reaches zero.

In other words:

- `async` schedules work
- `sync()` waits for that work to complete

This means that if you want a function to guarantee that all async work it started has finished before returning, that function should call `sync()` before it completes.

### How the compiler lowers async/sync

The compiler does not implement threads directly in the parser or AST. Instead:

- `sync()` lowers to the runtime symbol `__compiler_sync_tasks`
- `async f(...)` lowers to the runtime symbol `__compiler_async_call`
- the compiler generates a private wrapper function and a heap payload
- the wrapper unpacks the payload, calls the original function, and frees the payload memory when the task finishes

Example:

```text
async printd(99)
```

is lowered conceptually to:

```cpp
__compiler_async_call(__compiler_async_wrapper_N, payload_ptr);
```

The wrapper exists because the runtime only knows how to execute one generic task shape:

```cpp
void task(void *data)
```

but source-language functions have many different signatures such as `double(double)` or `double(double, double, double)`. The wrapper adapts the generic runtime ABI to the real callee signature.

The payload uses heap allocation instead of stack allocation because async work may outlive the current function call. A stack allocation created in the caller would be invalid once that caller returned.

### Async code flow

For a source expression like:

```text
async printd(99)
```

the current implementation follows this flow:

1. The parser builds an `AsyncExprAST` with callee `printd` and one argument expression `99`.
2. Codegen resolves `printd` to an LLVM function.
3. Codegen evaluates `99` and stores it in a heap payload.
4. Codegen generates a wrapper function with the generic shape `void wrapper(void *data)`.
5. That wrapper casts the payload back to the expected layout, loads the argument values, calls `printd`, and frees the heap payload.
6. Codegen emits a call to `__compiler_async_call(wrapper, payload_ptr)`.
7. The runtime enqueues that `(wrapper, payload_ptr)` pair onto the worker pool.
8. A worker thread later runs `wrapper(payload_ptr)`.
9. When the task completes, the runtime decrements its pending task count.
10. `sync()` blocks until that pending task count reaches `0`.

### Worker-pool design

The runtime uses a general worker-pool pattern:

- a shared queue stores pending tasks
- a fixed set of worker threads repeatedly pulls tasks from that queue
- a mutex protects the queue and bookkeeping state
- one condition variable wakes workers when new work arrives
- another condition variable lets `sync()` sleep until all tasks are finished

Each `async` expression creates exactly one queued task in the current implementation.

The runtime tracks a `pendingTasks` counter:

- enqueueing a task increments the counter
- finishing a task decrements the counter
- `sync()` waits until the counter reaches `0`

### Why a C++ runtime is needed

This separation between compiler and runtime is standard. The compiler can describe asynchronous work in LLVM IR, but the actual thread management requires native support for:

- worker threads
- mutexes
- condition variables
- queues
- shutdown/join behavior

Those are implemented in C++ in `runtime.cpp`, and the generated object file links against `runtime.o`.

### Runtime limitations

The async/sync support is intentionally small and concrete:

- `async` only supports direct named function calls
- all values are still `double`
- async return values are not propagated back into the language
- `sync()` waits for all outstanding async tasks in the process-wide runtime
- there is no task handle, future, or per-task join yet
- there is no closure/environment capture support yet

These limits keep the ABI (Application Binary Interface) and runtime understandable.

## Tokens and Keywords

Recognized keywords:

- `def`
- `extern`
- `binary`
- `unary`
- `if`
- `then`
- `else`
- `for`
- `in`
- `var`
- `async`
- `sync`

Comments start with `#` and continue to the end of the line.

## Tests

The repository includes [`tests/full_coverage.cmp`](/tests/full_coverage.cmp), a source file designed to exercise the implemented language features end to end.

Run the compile-only integration pass with:

```sh
make test-compile
```

Run the full correctness suite with:

```sh
make test-correctness
```

Run both with:

```sh
make test
```

`test-correctness` compiles `tests/full_coverage.cmp` to `tests/full_coverage.o`, links it with [`tests/runtime_driver.cpp`](/tests/runtime_driver.cpp) and [`runtime.cpp`](/runtime.cpp), then executes runtime assertions against the generated code.

## General Project Constraints

- Input can come from either `stdin` or `--file <path>`.
- `--stdin` writes `output.o`; `--file path/to/file.cmp` writes `path/to/file.o`.
- All values are compiled as `double`.
- The compiler emits an object file but does not link a standalone executable by itself.
- Whitespace, including spaces, tabs, and newlines, is treated as a separator and is mostly only needed to keep tokens from running together.
- The lexer accepts only alphanumeric identifier characters after the first letter.
