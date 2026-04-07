#pragma once

#include "SourceLocation.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class AllocaInst;
} // namespace llvm

using namespace llvm;

namespace Compiler {

extern std::unique_ptr<LLVMContext> theContext;
extern std::unique_ptr<Module> theModule;
extern std::unique_ptr<IRBuilder<>> builder;
extern std::map<std::string, AllocaInst *> namedValues;

// Base class
class ExprAST {
  SourceLocation loc;

public:
  explicit ExprAST(SourceLocation loc) : loc(loc) {}
  virtual ~ExprAST() = default;
  SourceLocation getLoc() const { return loc; }
  virtual Value *codegen() = 0;
};

class NumberExprAST : public ExprAST {
  double val;

public:
  NumberExprAST(double val, SourceLocation loc) : ExprAST(loc), val(val) {}
  double getValue() const { return val; }
  Value *codegen() override;
};

class VariableExprAST : public ExprAST {
  std::string name;

public:
  VariableExprAST(const std::string &name, SourceLocation loc)
      : ExprAST(loc), name(name) {}
  const std::string &getName() const { return name; }
  Value *codegen() override;
};

class UnaryExprAST : public ExprAST {
  char op;
  std::unique_ptr<ExprAST> operand;

public:
  UnaryExprAST(char op, std::unique_ptr<ExprAST> operand, SourceLocation loc)
      : ExprAST(loc), op(op), operand(std::move(operand)) {}
  char getOperator() const { return op; }
  const ExprAST *getOperand() const { return operand.get(); }
  std::unique_ptr<ExprAST> takeOperand() { return std::move(operand); }
  Value *codegen() override;
};

class BinaryExprAST : public ExprAST {
  char op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS, SourceLocation loc)
      : ExprAST(loc), op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
  char getOperator() const { return op; }
  const ExprAST *getLHS() const { return LHS.get(); }
  const ExprAST *getRHS() const { return RHS.get(); }
  std::unique_ptr<ExprAST> takeLHS() { return std::move(LHS); }
  std::unique_ptr<ExprAST> takeRHS() { return std::move(RHS); }
  Value *codegen() override;
};

class VarExprAST : public ExprAST {
  std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> varNames;
  std::unique_ptr<ExprAST> body;

public:
  VarExprAST(std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> vars,
             std::unique_ptr<ExprAST> body, SourceLocation loc)
      : ExprAST(loc), varNames(std::move(vars)), body(std::move(body)) {}
  const auto &getVarNames() const { return varNames; }
  const ExprAST *getBody() const { return body.get(); }
  auto takeVarNames() { return std::move(varNames); }
  std::unique_ptr<ExprAST> takeBody() { return std::move(body); }
  Value *codegen() override;
};

class IfExprAST : public ExprAST {
  std::unique_ptr<ExprAST> condExpr;
  std::unique_ptr<ExprAST> thenExpr;
  std::unique_ptr<ExprAST> elseExpr;

public:
  IfExprAST(std::unique_ptr<ExprAST> condExpr,
            std::unique_ptr<ExprAST> thenExpr,
            std::unique_ptr<ExprAST> elseExpr, SourceLocation loc)
      : ExprAST(loc), condExpr(std::move(condExpr)),
        thenExpr(std::move(thenExpr)), elseExpr(std::move(elseExpr)) {}
  const ExprAST *getCondExpr() const { return condExpr.get(); }
  const ExprAST *getThenExpr() const { return thenExpr.get(); }
  const ExprAST *getElseExpr() const { return elseExpr.get(); }
  std::unique_ptr<ExprAST> takeCondExpr() { return std::move(condExpr); }
  std::unique_ptr<ExprAST> takeThenExpr() { return std::move(thenExpr); }
  std::unique_ptr<ExprAST> takeElseExpr() { return std::move(elseExpr); }
  Value *codegen() override;
};

class ForExprAST : public ExprAST {
  std::string varName;
  std::unique_ptr<ExprAST> startExpr;
  std::unique_ptr<ExprAST> endExpr;
  std::unique_ptr<ExprAST> stepExpr;
  std::unique_ptr<ExprAST> body;

public:
  ForExprAST(const std::string &varName, std::unique_ptr<ExprAST> startExpr,
             std::unique_ptr<ExprAST> endExpr,
             std::unique_ptr<ExprAST> stepExpr, std::unique_ptr<ExprAST> body,
             SourceLocation loc)
      : ExprAST(loc), varName(varName), startExpr(std::move(startExpr)),
        endExpr(std::move(endExpr)), stepExpr(std::move(stepExpr)),
        body(std::move(body)) {}
  const std::string &getVarName() const { return varName; }
  const ExprAST *getStartExpr() const { return startExpr.get(); }
  const ExprAST *getEndExpr() const { return endExpr.get(); }
  const ExprAST *getStepExpr() const { return stepExpr.get(); }
  const ExprAST *getBody() const { return body.get(); }
  std::unique_ptr<ExprAST> takeStartExpr() { return std::move(startExpr); }
  std::unique_ptr<ExprAST> takeEndExpr() { return std::move(endExpr); }
  std::unique_ptr<ExprAST> takeStepExpr() { return std::move(stepExpr); }
  std::unique_ptr<ExprAST> takeBody() { return std::move(body); }
  Value *codegen() override;
};

class ParForExprAST : public ExprAST {
  std::string varName;
  std::unique_ptr<ExprAST> startExpr;
  std::unique_ptr<ExprAST> endExpr;
  std::unique_ptr<ExprAST> stepExpr;
  std::unique_ptr<ExprAST> body;

public:
  ParForExprAST(const std::string &varName, std::unique_ptr<ExprAST> startExpr,
                std::unique_ptr<ExprAST> endExpr,
                std::unique_ptr<ExprAST> stepExpr,
                std::unique_ptr<ExprAST> body, SourceLocation loc)
      : ExprAST(loc), varName(varName), startExpr(std::move(startExpr)),
        endExpr(std::move(endExpr)), stepExpr(std::move(stepExpr)),
        body(std::move(body)) {}
  const std::string &getVarName() const { return varName; }
  const ExprAST *getStartExpr() const { return startExpr.get(); }
  const ExprAST *getEndExpr() const { return endExpr.get(); }
  const ExprAST *getStepExpr() const { return stepExpr.get(); }
  const ExprAST *getBody() const { return body.get(); }
  std::unique_ptr<ExprAST> takeStartExpr() { return std::move(startExpr); }
  std::unique_ptr<ExprAST> takeEndExpr() { return std::move(endExpr); }
  std::unique_ptr<ExprAST> takeStepExpr() { return std::move(stepExpr); }
  std::unique_ptr<ExprAST> takeBody() { return std::move(body); }
  Value *codegen() override;
};

// Function calls (CallExpr is industry standard for this case)
class CallExprAST : public ExprAST {
  std::string callee;
  std::vector<std::unique_ptr<ExprAST>> args;

public:
  CallExprAST(std::string &callee, std::vector<std::unique_ptr<ExprAST>> args,
              SourceLocation loc)
      : ExprAST(loc), callee(callee), args(std::move(args)) {}
  const std::string &getCallee() const { return callee; }
  const auto &getArgs() const { return args; }
  auto takeArgs() { return std::move(args); }
  Value *codegen() override;
};

class SyncExprAST : public ExprAST {
public:
  SyncExprAST(SourceLocation loc) : ExprAST(loc) {};
  Value *codegen() override;
};

class AsyncExprAST : public ExprAST {
  std::string callee;
  std::vector<std::unique_ptr<ExprAST>> args;

public:
  AsyncExprAST(std::string &callee, std::vector<std::unique_ptr<ExprAST>> args,
               SourceLocation loc)
      : ExprAST(loc), callee(callee), args(std::move(args)) {}
  const std::string &getCallee() const { return callee; }
  const auto &getArgs() const { return args; }
  auto takeArgs() { return std::move(args); }
  Value *codegen() override;
};

// Prototype of a function
// Captures name and argument names
// (thus implicitly the number of arguments the function takes)
class PrototypeAST {
  std::string name;
  std::vector<std::string> args;
  bool isOperator;
  unsigned precedence;
  SourceLocation loc;

public:
  PrototypeAST(const std::string &name, std::vector<std::string> args,
               bool isOperator = false, unsigned precedence = 0,
               SourceLocation loc = {1, 1})
      : name(name), args(std::move(args)), isOperator(isOperator),
        precedence(precedence), loc(loc) {}
  const std::vector<std::string> &getArgs() const { return args; }
  std::unique_ptr<PrototypeAST> clone() const {
    return std::make_unique<PrototypeAST>(name, args, isOperator, precedence,
                                          loc);
  }
  Function *codegen();
  const std::string &getName() const { return name; }
  std::string getSymbolName() const;
  SourceLocation getLoc() const { return loc; }
  bool isUnaryOp() const { return isOperator && name.substr(0, 5) == "unary"; }
  bool isBinaryOp() const {
    return isOperator && name.substr(0, 6) == "binary";
  }
  char getOperatorName() const { return name[name.size() - 1]; }
  unsigned getBinaryPrecedence() const { return precedence; }
};

class FunctionAST {
  std::unique_ptr<PrototypeAST> prototype;
  std::unique_ptr<ExprAST> body;

public:
  FunctionAST(std::unique_ptr<PrototypeAST> prototype,
              std::unique_ptr<ExprAST> body)
      : prototype(std::move(prototype)), body(std::move(body)) {}
  const PrototypeAST &getProto() const { return *prototype; }
  const ExprAST *getBody() const { return body.get(); }
  std::unique_ptr<ExprAST> takeBody() { return std::move(body); }
  Function *codegen();
};

extern std::map<std::string, std::unique_ptr<PrototypeAST>> functionProtos;
void initializeDebugInfo(const std::string &sourceName);
void finalizeDebugInfo();

}; // namespace Compiler
