#pragma once

#include <memory>

namespace llvm {
class Value;
class Function;
} // namespace llvm

namespace Compiler {

class ExprAST;
class PrototypeAST;

std::unique_ptr<ExprAST> logError(const char *str);
std::unique_ptr<PrototypeAST> logErrorP(const char *str);
llvm::Value *logErrorV(const char *str);
llvm::Function *logErrorF(const char *str);

} // namespace Compiler