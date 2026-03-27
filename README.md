# Compiler

This project is a small LLVM-based compiler for an expression-oriented language. It parses source code into an AST, lowers it to LLVM IR, and emits an object file named `output.o`.

## Requirements

- Homebrew LLVM installed
- `clang++` from that LLVM toolchain
- `llvm-config` available on `PATH`

Install the dependency on macOS with:

```sh
brew install llvm
```

## Build

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

Debug mode:

```sh
make run-dev
```

### File mode

This reads directly from a source file and runs without interactive prompts:

```sh
./main --file tests/full_coverage.cmp
```

This always writes `output.o` in the project root when compilation succeeds.

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
- User-defined unary operators
- User-defined binary operators with custom precedence
- `#` line comments
- Optional debug logging and LLVM IR dumps
- Debug metadata emission into the generated object file

## Syntax Guide

If you just want the most common source forms, start here:

```text
def add(x y) x + y
extern sin(x)
add(1, 2)
if x < y then x else y
for i = 1, i < 10, 1 in i * 2
var a = 1, b = 2 in a + b
def unary!(x) 0 - x
def binary% 50 (x y) x - y
```

Notes:

- `for i = 1, i < 10 in i` is also valid; the step expression is optional
- `var x in ...` is valid; omitted initializers default to `0.0`
- custom binary operators default to precedence `30` if no number is provided

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

Examples:

```text
def add(x y) x + y
extern sin(x)
add(1, 2)
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
```

Examples:

```text
42
x
(a + b) * 2
```

### Identifiers and function calls

An identifier can be either:

- a variable reference like `x`
- a function call like `add(1, 2)`

```text
identifierexpr ::= identifier
                 | identifier '(' expression (',' expression)* ')'
```

Examples:

```text
radius
sin(angle)
add(1, 2)
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

The third expression in a `for` loop is the optional step value.

Examples:

```text
if x < y then x else y
for i = 1, i < 10, 1 in i * 2
for i = 1, i < 10 in i
```

### Local variables

A `var` expression introduces one or more local bindings, then evaluates a body expression after `in`.

```text
varexpr ::= 'var' identifier ('=' expression)?
            (',' identifier ('=' expression)?)* 'in' expression
```

If a binding has no initializer, it defaults to `0.0`.

Examples:

```text
var a = 1 in a + 2
var a = 1, b = 2 in a + b
var x, y = 3 in x + y
```

### Function signatures

A prototype describes the name and parameters of a function or operator.

```text
prototype ::= identifier '(' identifier* ')'
            | 'unary' ASCII '(' identifier ')'
            | 'binary' ASCII number? '(' identifier identifier ')'
```

Examples:

```text
add(x y)
unary!(x)
binary% 50 (x y)
binary^ (x y)
```

Notes:

- `identifier*` means zero or more parameter names
- `number?` after a binary operator means operator precedence is optional
- if no precedence is given for a custom binary operator, the parser uses `30`

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

Comments start with `#` and continue to the end of the line.

## Debug Mode

Enable debug logging with either:

```sh
./main --dev --file tests/full_coverage.cmp
```

or:

```sh
COMPILER_DEV=1 ./main --file tests/full_coverage.cmp
```

Debug mode traces lexer/parser/codegen activity and prints generated LLVM IR for parsed functions.

## Test Input

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

`test-correctness` compiles `tests/full_coverage.cmp` to `output.o`, links it with [`tests/runtime_driver.cpp`](/tests/runtime_driver.cpp), and executes runtime assertions against the generated code.

## Notes and Limitations

- Input can come from either `stdin` or `--file <path>`.
- All values are compiled as `double`.
- The compiler emits an object file but does not link an executable.
- Whitespace, including spaces, tabs, and newlines, is treated as a separator and is mostly only needed to keep tokens from running together.
- The lexer accepts only alphanumeric identifier characters after the first letter.
- The parser installs built-in precedences only for `<`, `+`, `-`, and `*`.
