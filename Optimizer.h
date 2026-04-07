#pragma once

#include <memory>

namespace Compiler {

class ExprAST;

std::unique_ptr<ExprAST> optimizeExpr(std::unique_ptr<ExprAST> expr);

} // namespace Compiler
