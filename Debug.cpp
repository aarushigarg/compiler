#include "Debug.h"

#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Compiler {

static bool devMode = false;

void initDevModeFromArgs(int argc, char **argv) {
  if (devMode) {
    return;
  }

  // Allow an env var to force dev logging on
  const char *envDev = std::getenv("COMPILER_DEV");
  if (envDev && std::strcmp(envDev, "0") != 0) {
    devMode = true;
    return;
  }

  // Parse CLI flags
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--dev") == 0 || std::strcmp(argv[i], "-d") == 0) {
      devMode = true;
      break;
    }
  }
}

bool isDevMode() { return devMode; }

void devPrintf(const char *format, ...) {
  if (!devMode) {
    return;
  }
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

void devPrintIR(const char *label, llvm::Function *func) {
  if (!devMode || !func) {
    return;
  }
  fprintf(stderr, "%s", label);
  func->print(llvm::errs());
  fprintf(stderr, "\n");
}

} // namespace Compiler
