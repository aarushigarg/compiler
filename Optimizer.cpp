#include "Optimizer.h"

#include "AbstractSyntaxTree.h"

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace Compiler {

namespace {

double foldBinaryConstant(char op, double lhs, double rhs, bool &folded) {
  folded = true;
  switch (op) {
  case '+':
    return lhs + rhs;
  case '-':
    return lhs - rhs;
  case '*':
    return lhs * rhs;
  case '<':
    return lhs < rhs ? 1.0 : 0.0;
  default:
    folded = false;
    return 0.0;
  }
}

std::unique_ptr<ExprAST> optimizeBinaryExpr(std::unique_ptr<ExprAST> expr) {
  auto *binaryExpr = dynamic_cast<BinaryExprAST *>(expr.get());
  if (!binaryExpr) {
    return expr;
  }

  SourceLocation loc = binaryExpr->getLoc();
  char op = binaryExpr->getOperator();
  std::unique_ptr<ExprAST> lhs = optimizeExpr(binaryExpr->takeLHS());
  std::unique_ptr<ExprAST> rhs = optimizeExpr(binaryExpr->takeRHS());

  auto *lhsNumber = dynamic_cast<NumberExprAST *>(lhs.get());
  auto *rhsNumber = dynamic_cast<NumberExprAST *>(rhs.get());
  if (lhsNumber && rhsNumber) {
    bool folded = false;
    double foldedValue = foldBinaryConstant(op, lhsNumber->getValue(),
                                            rhsNumber->getValue(), folded);
    if (folded) {
      return std::make_unique<NumberExprAST>(foldedValue, loc);
    }
  }

  return std::make_unique<BinaryExprAST>(op, std::move(lhs), std::move(rhs),
                                         loc);
}

} // namespace

std::unique_ptr<ExprAST> optimizeExpr(std::unique_ptr<ExprAST> expr) {
  if (!expr) {
    return nullptr;
  }

  if (dynamic_cast<NumberExprAST *>(expr.get()) ||
      dynamic_cast<VariableExprAST *>(expr.get()) ||
      dynamic_cast<SyncExprAST *>(expr.get())) {
    return expr;
  }

  if (dynamic_cast<BinaryExprAST *>(expr.get())) {
    return optimizeBinaryExpr(std::move(expr));
  }

  if (auto *unaryExpr = dynamic_cast<UnaryExprAST *>(expr.get())) {
    SourceLocation loc = unaryExpr->getLoc();
    char op = unaryExpr->getOperator();
    std::unique_ptr<ExprAST> operand =
        optimizeExpr(unaryExpr->takeOperand());
    return std::make_unique<UnaryExprAST>(op, std::move(operand), loc);
  }

  if (auto *ifExpr = dynamic_cast<IfExprAST *>(expr.get())) {
    SourceLocation loc = ifExpr->getLoc();
    std::unique_ptr<ExprAST> condExpr =
        optimizeExpr(ifExpr->takeCondExpr());
    std::unique_ptr<ExprAST> thenExpr =
        optimizeExpr(ifExpr->takeThenExpr());
    std::unique_ptr<ExprAST> elseExpr =
        optimizeExpr(ifExpr->takeElseExpr());
    return std::make_unique<IfExprAST>(std::move(condExpr), std::move(thenExpr),
                                       std::move(elseExpr), loc);
  }

  if (auto *varExpr = dynamic_cast<VarExprAST *>(expr.get())) {
    SourceLocation loc = varExpr->getLoc();
    auto vars = varExpr->takeVarNames();
    for (auto &var : vars) {
      var.second = optimizeExpr(std::move(var.second));
    }
    std::unique_ptr<ExprAST> body = optimizeExpr(varExpr->takeBody());
    return std::make_unique<VarExprAST>(std::move(vars), std::move(body), loc);
  }

  if (auto *forExpr = dynamic_cast<ForExprAST *>(expr.get())) {
    SourceLocation loc = forExpr->getLoc();
    std::string varName = forExpr->getVarName();
    std::unique_ptr<ExprAST> startExpr =
        optimizeExpr(forExpr->takeStartExpr());
    std::unique_ptr<ExprAST> endExpr = optimizeExpr(forExpr->takeEndExpr());
    std::unique_ptr<ExprAST> stepExpr = optimizeExpr(forExpr->takeStepExpr());
    std::unique_ptr<ExprAST> body = optimizeExpr(forExpr->takeBody());
    return std::make_unique<ForExprAST>(varName, std::move(startExpr),
                                        std::move(endExpr), std::move(stepExpr),
                                        std::move(body), loc);
  }

  if (auto *parForExpr = dynamic_cast<ParForExprAST *>(expr.get())) {
    SourceLocation loc = parForExpr->getLoc();
    std::string varName = parForExpr->getVarName();
    std::unique_ptr<ExprAST> startExpr =
        optimizeExpr(parForExpr->takeStartExpr());
    std::unique_ptr<ExprAST> endExpr =
        optimizeExpr(parForExpr->takeEndExpr());
    std::unique_ptr<ExprAST> stepExpr =
        optimizeExpr(parForExpr->takeStepExpr());
    std::unique_ptr<ExprAST> body = optimizeExpr(parForExpr->takeBody());
    return std::make_unique<ParForExprAST>(
        varName, std::move(startExpr), std::move(endExpr), std::move(stepExpr),
        std::move(body), loc);
  }

  if (auto *callExpr = dynamic_cast<CallExprAST *>(expr.get())) {
    SourceLocation loc = callExpr->getLoc();
    std::string callee = callExpr->getCallee();
    auto args = callExpr->takeArgs();
    for (auto &arg : args) {
      arg = optimizeExpr(std::move(arg));
    }
    return std::make_unique<CallExprAST>(callee, std::move(args), loc);
  }

  if (auto *asyncExpr = dynamic_cast<AsyncExprAST *>(expr.get())) {
    SourceLocation loc = asyncExpr->getLoc();
    std::string callee = asyncExpr->getCallee();
    auto args = asyncExpr->takeArgs();
    for (auto &arg : args) {
      arg = optimizeExpr(std::move(arg));
    }
    return std::make_unique<AsyncExprAST>(callee, std::move(args), loc);
  }

  return expr;
}

} // namespace Compiler
