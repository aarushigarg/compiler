#include "AbstractSyntaxTree.h"

#include "Debug.h"
#include "LogErrors.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;

namespace Compiler {

std::unique_ptr<LLVMContext> theContext;
std::unique_ptr<Module> theModule;
std::unique_ptr<IRBuilder<>> builder;
std::map<std::string, Value *> namedValues;
std::map<std::string, std::unique_ptr<PrototypeAST>> functionProtos;

static Function *getFunction(const std::string &name) {
  devPrintf("Codegen: lookup function '%s'\n", name.c_str());
  if (auto *func = theModule->getFunction(name)) {
    return func;
  }

  auto iter = functionProtos.find(name);
  if (iter != functionProtos.end()) {
    return iter->second->codegen();
  }

  return nullptr;
}

Value *NumberExprAST::codegen() {
  devPrintf("Codegen: number %f\n", val);
  return ConstantFP::get(*theContext, APFloat(val));
}

Value *VariableExprAST::codegen() {
  devPrintf("Codegen: variable %s\n", name.c_str());
  // Look up variable name
  Value *V = namedValues[name];
  if (!V) {
    return logErrorV(("Unknown variable name: " + name).c_str());
  }
  return V;
}

Value *BinaryExprAST::codegen() {
  devPrintf("Codegen: binary '%c'\n", op);
  Value *L = LHS->codegen();
  Value *R = RHS->codegen();
  if (!L || !R) {
    return nullptr;
  }

  switch (op) {
  case '+':
    return builder->CreateFAdd(L, R, "addtmp");
  case '-':
    return builder->CreateFSub(L, R, "subtmp");
  case '*':
    return builder->CreateFMul(L, R, "multmp");
  case '<':
    L = builder->CreateFCmpULT(L, R, "cmptmp");
    // Convert bool to double 0.0 or 1.0
    return builder->CreateUIToFP(L, Type::getDoubleTy(*theContext), "booltmp");
  default:
    return logErrorV("invalid binary operator");
  }
}

Value *CallExprAST::codegen() {
  devPrintf("Codegen: call %s (%zu args)\n", callee.c_str(), args.size());
  // Look up name in golbal module table
  Function *calleeF = getFunction(callee);
  if (!calleeF) {
    return logErrorV(("Unknown function referenced: " + callee).c_str());
  }
  if (calleeF->arg_size() != args.size()) {
    return logErrorV("Incorrect number of arguments passed");
  }

  std::vector<Value *> ArgsV;
  for (unsigned i = 0, e = args.size(); i != e; ++i) {
    ArgsV.push_back(args[i]->codegen());
    if (!ArgsV.back()) {
      return nullptr;
    }
  }

  return builder->CreateCall(calleeF, ArgsV, "calltmp");
}

Function *PrototypeAST::codegen() {
  devPrintf("Codegen: prototype %s (%zu args)\n", name.c_str(), args.size());
  // Function with return type double and arguments of type double
  std::vector<Type *> Doubles(args.size(), Type::getDoubleTy(*theContext));
  FunctionType *funcType =
      FunctionType::get(Type::getDoubleTy(*theContext), Doubles, false);

  // Ensure existing function has matching signature
  Function *func;
  if ((func = theModule->getFunction(name))) {
    if (func->getFunctionType() != funcType) {
      logErrorP("Function signature mismatch");
      return nullptr;
    }
  } else {
    func = Function::Create(funcType, Function::ExternalLinkage, name,
                            theModule.get());
  }

  // Set names for all arguments
  unsigned idx = 0;
  for (auto &arg : func->args()) {
    arg.setName(args[idx++]);
  }

  return func;
}

Function *FunctionAST::codegen() {
  devPrintf("Codegen: function %s\n", prototype->getName().c_str());
  // First check for existing function from previous 'extern' declaration
  Function *func = theModule->getFunction(prototype->getName());

  if (!func) {
    func = prototype->codegen();
  }
  if (!func) {
    return nullptr;
  }

  // Prevent redefinition if the function already has a body
  if (!func->empty()) {
    return logErrorF("Function already defined");
  }

  // Create a new basic block to start insertion into
  BasicBlock *basicBlock = BasicBlock::Create(*theContext, "entry", func);
  builder->SetInsertPoint(basicBlock);

  // Record the function arguments in the named values map
  namedValues.clear();
  for (auto &arg : func->args()) {
    namedValues[arg.getName().str()] = &arg;
  }

  if (Value *retVal = body->codegen()) {
    // Finish the function by creating ret
    builder->CreateRet(retVal);

    // Validate the generated function
    verifyFunction(*func);

    return func;
  }

  // Error reading body, remove function
  func->eraseFromParent();
  return nullptr;
}

} // namespace Compiler
