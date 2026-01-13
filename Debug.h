#pragma once

namespace llvm {
class Function;
} // namespace llvm

namespace Compiler {

void initDevModeFromArgs(int argc, char **argv);
bool isDevMode();

void devPrintf(const char *format, ...);
void devPrintIR(const char *label, llvm::Function *func);

} // namespace Compiler
