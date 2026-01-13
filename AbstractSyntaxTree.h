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

class BinaryExprAST : public ExprAST {
  char op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char op, std::unique_ptr<ExprAST> LHS,
                std::unique_ptr<ExprAST> RHS)
      : op(op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
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

public:
  PrototypeAST(const std::string &name, std::vector<std::string> args)
      : name(name), args(std::move(args)) {}
  const std::vector<std::string> &getArgs() const { return args; }
  std::unique_ptr<PrototypeAST> clone() const {
    return std::make_unique<PrototypeAST>(name, args);
  }
  Function *codegen();
  const std::string &getName() const { return name; }
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
