#pragma once

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;

namespace Compiler {

extern std::unique_ptr<LLVMContext> theContext;
extern std::unique_ptr<Module> theModule;
extern std::unique_ptr<IRBuilder<>> builder;
extern std::map<std::string, Value *> namedValues;

// Base class
class ExprAST {
public:
  virtual ~ExprAST() = default;
  virtual Value *codegen() = 0;
};

class NumberExprAST : public ExprAST {
  double val;

public:
  NumberExprAST(double val) : val(val) {}
  Value *codegen() override;
};

class VariableExprAST : public ExprAST {
  std::string name;

public:
  VariableExprAST(const std::string &name) : name(name) {}
  Value *codegen() override;
};

class UnaryExprAST : public ExprAST {
  char op;
  std::unique_ptr<ExprAST> operand;

public:
  UnaryExprAST(char op, std::unique_ptr<ExprAST> operand)
      : op(op), operand(std::move(operand)) {}
  Value *codegen() override;
};

class BinaryExprAST : public ExprAST {
  char op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
  Value *codegen() override;
};

class IfExprAST : public ExprAST {
  std::unique_ptr<ExprAST> condExpr;
  std::unique_ptr<ExprAST> thenExpr;
  std::unique_ptr<ExprAST> elseExpr;

public:
  IfExprAST(std::unique_ptr<ExprAST> condExpr,
            std::unique_ptr<ExprAST> thenExpr,
            std::unique_ptr<ExprAST> elseExpr)
      : condExpr(std::move(condExpr)), thenExpr(std::move(thenExpr)),
        elseExpr(std::move(elseExpr)) {}
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
             std::unique_ptr<ExprAST> stepExpr,
             std::unique_ptr<ExprAST> body)
      : varName(varName), startExpr(std::move(startExpr)),
        endExpr(std::move(endExpr)), stepExpr(std::move(stepExpr)),
        body(std::move(body)) {}
  Value *codegen() override;
};

// Function calls (CallExpr is industry standard for this case)
class CallExprAST : public ExprAST {
  std::string callee;
  std::vector<std::unique_ptr<ExprAST>> args;

public:
  CallExprAST(std::string &callee, std::vector<std::unique_ptr<ExprAST>> args)
      : callee(callee), args(std::move(args)) {}
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

public:
  PrototypeAST(const std::string &name, std::vector<std::string> args,
               bool isOperator = false, unsigned precedence = 0)
      : name(name), args(std::move(args)), isOperator(isOperator),
        precedence(precedence) {}
  const std::vector<std::string> &getArgs() const { return args; }
  std::unique_ptr<PrototypeAST> clone() const {
    return std::make_unique<PrototypeAST>(name, args, isOperator, precedence);
  }
  Function *codegen();
  const std::string &getName() const { return name; }
  bool isUnaryOp() const { return isOperator && name.substr(0, 5) == "unary"; }
  bool isBinaryOp() const { return isOperator && name.substr(0, 6) == "binary"; }
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
  Function *codegen();
};

extern std::map<std::string, std::unique_ptr<PrototypeAST>> functionProtos;

}; // namespace Compiler
