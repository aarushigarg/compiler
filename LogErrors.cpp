#include "LogErrors.h"
#include "AbstractSyntaxTree.h"

#include <cstdio>

namespace Compiler {

std::unique_ptr<ExprAST> logError(const char *str) {
  fprintf(stderr, "Error: %s\n", str);
  return nullptr;
}

std::unique_ptr<PrototypeAST> logErrorP(const char *str) {
  logError(str);
  return nullptr;
}

llvm::Value *logErrorV(const char *str) {
  logError(str);
  return nullptr;
}

llvm::Function *logErrorF(const char *str) {
  logError(str);
  return nullptr;
}

} // namespace Compiler