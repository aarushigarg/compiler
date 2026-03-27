# Compiler

LLVM-based compiler for an expression-oriented language with task-based `async`/`sync` support and a C++ worker-pool runtime.

The compiler parses source code into an AST, lowers it to LLVM IR, emits an object file, and links cleanly against a native runtime that executes async work on background threads.

The language is still intentionally simple: every runtime value is currently represented as a `double`, which keeps the IR and async payload layout straightforward.

## What I Built

- Front end for an expression language with functions, calls, conditionals, loops, local bindings, and user-defined operators
- LLVM IR code generation and object-file emission
- Language-level `async` and `sync()` constructs
- Generic async lowering through:
  - a generated wrapper function per async site
  - a heap payload carrying async arguments
  - a single runtime ABI entry point, `__compiler_async_call`
- C++ runtime with a general worker-pool design:
  - shared task queue
  - worker threads
  - condition-variable wakeups
  - barrier-style `sync()` using a pending-task counter
- End-to-end correctness tests that compile source code, link generated objects, and run assertions against the result

## Implementation Highlights

### Async lowering

`async f(args...)` is not compiled as a direct function call. Instead, the compiler:

1. evaluates the async argument expressions
2. stores those values in a heap payload
3. generates a private wrapper function with the generic shape `void wrapper(void *data)`
4. emits a call to the runtime entry point:

```cpp
__compiler_async_call(__compiler_async_wrapper_N, payload_ptr);
```

### Why wrappers are used

The runtime executes one generic task shape:

```cpp
void task(void *data)
```

But source-language functions have many different signatures such as:

- `double(double)`
- `double(double, double)`
- `double(double, double, double, double)`

The wrapper adapts the generic runtime ABI to the real callee signature by:

1. casting the payload pointer back to the expected layout
2. loading the stored argument values
3. calling the original function
4. freeing the heap payload after use

### Why the payload uses heap memory

Async work may outlive the current function call. A stack allocation made in the caller would become invalid once that caller returned, so the async arguments are stored in heap memory instead.

### Runtime model

The runtime in [`runtime.cpp`](/runtime.cpp) uses a standard worker-pool pattern:

- a shared queue stores pending tasks
- a fixed set of worker threads repeatedly pulls tasks from that queue
- a mutex protects shared state
- one condition variable wakes workers when new work arrives
- another condition variable lets `sync()` block until all queued/running work completes

`sync()` acts as a barrier by waiting until the runtime's pending-task count reaches zero.

## Async Code Flow

For a source expression like:

```text
async printd(99)
```

the code flow is:

1. The parser builds an `AsyncExprAST` with callee `printd` and argument `99`.
2. Codegen resolves `printd` to an LLVM function.
3. Codegen evaluates `99` and stores it in a heap payload.
4. Codegen generates a wrapper function for that async site.
5. Codegen emits `__compiler_async_call(wrapper, payload_ptr)`.
6. The runtime enqueues that `(wrapper, payload_ptr)` pair.
7. A worker thread later executes `wrapper(payload_ptr)`.
8. The wrapper loads the arguments, calls `printd(99)`, and frees the payload.
9. The runtime decrements its pending-task counter.
10. `sync()` blocks until that counter reaches `0`.

## Build

## Requirements

- Homebrew LLVM installed
- `clang++` from that LLVM toolchain
- `llvm-config` available on `PATH`

Install LLVM on macOS with:

```sh
brew install llvm
```

### Build the compiler

```sh
make
```

This produces the compiler executable `main`.

### Compile a source file

```sh
./main --file tests/full_coverage.cmp
```

That emits `tests/full_coverage.o`.

The compiler stops at object-file generation. The test flow links that object against the runtime and driver code.

### Interactive mode

```sh
make run
```

or:

```sh
./main --stdin
```

## Tests

The repository includes [`tests/full_coverage.cmp`](/tests/full_coverage.cmp), which exercises the implemented language features end to end.

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

`test-correctness` compiles [`tests/full_coverage.cmp`](/tests/full_coverage.cmp) to `tests/full_coverage.o`, links it with [`tests/runtime_driver.cpp`](/tests/runtime_driver.cpp) and [`runtime.cpp`](/runtime.cpp), and executes runtime assertions against the generated code.

## Language Reference

This section is the usage-oriented reference for the language itself.

### Language Features

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

### Quick Syntax Guide

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
- all language values are currently compiled as `double`

### Grammar Overview

#### How to read the grammar

- `::=` means "is defined as"
- `|` means "or"
- `(...)` groups parts together
- `?` means "optional"
- `*` means "zero or more"

#### Top-level input

```text
top ::= definition
     | external
     | expression
```

#### Expressions

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

#### Identifiers and function calls

```text
identifierexpr ::= identifier
                 | identifier '(' expression (',' expression)* ')'
```

#### Control flow

```text
ifexpr ::= 'if' expression 'then' expression 'else' expression

forexpr ::= 'for' identifier '=' expression ','
            expression
            (',' expression)?
            'in' expression
```

#### Local variables

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

#### Async and sync

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
- `async` currently works only with calls of the form `async functionName(...)`
- `async` does not return the callee's result to the language; the expression itself evaluates to `0.0`
- `sync()` is a process-wide barrier for all outstanding async work

#### Function signatures

```text
prototype ::= identifier '(' identifier* ')'
            | 'unary' ASCII '(' identifier ')'
            | 'binary' ASCII number? '(' identifier identifier ')'
```

### Tokens and Keywords

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

## General Project Constraints

- Input can come from either `stdin` or `--file <path>`
- `--stdin` writes `output.o`; `--file path/to/file.cmp` writes `path/to/file.o`
- Whitespace, including spaces, tabs, and newlines, is treated as a separator and is mostly only needed to keep tokens from running together
- The lexer accepts only alphanumeric identifier characters after the first letter
