#pragma once

#include <memory>

namespace Compiler {

class ExprAST;
class PrototypeAST;
class FunctionAST;

std::unique_ptr<ExprAST> parseExpression();
std::unique_ptr<PrototypeAST> parsePrototype();
std::unique_ptr<FunctionAST> parseDefinition();
std::unique_ptr<PrototypeAST> parseExtern();
std::unique_ptr<FunctionAST> parseTopLevelExpr();

} // namespace Compiler