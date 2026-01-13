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
std::map<std::string, AllocaInst *> namedValues;
std::map<std::string, std::unique_ptr<PrototypeAST>> functionProtos;

static Function *getFunction(const std::string &name) {
  devPrintf("Codegen: lookup function '%s'\n", name.c_str());
  // Prefer existing definitions, then fall back to cached prototypes
  if (auto *func = theModule->getFunction(name)) {
    return func;
  }

  auto iter = functionProtos.find(name);
  if (iter != functionProtos.end()) {
    return iter->second->codegen();
  }

  return nullptr;
}

static AllocaInst *createEntryBlockAlloca(Function *func,
                                          const std::string &varName) {
  IRBuilder<> tmpBuilder(&func->getEntryBlock(),
                         func->getEntryBlock().begin());
  return tmpBuilder.CreateAlloca(Type::getDoubleTy(*theContext), nullptr,
                                 varName.c_str());
}

Value *NumberExprAST::codegen() {
  devPrintf("Codegen: number %f\n", val);
  return ConstantFP::get(*theContext, APFloat(val));
}

Value *VariableExprAST::codegen() {
  devPrintf("Codegen: variable %s\n", name.c_str());
  // Look up variable name
  AllocaInst *V = namedValues[name];
  if (!V) {
    return logErrorV(("Unknown variable name: " + name).c_str());
  }
  return builder->CreateLoad(Type::getDoubleTy(*theContext), V, name.c_str());
}

Value *UnaryExprAST::codegen() {
  devPrintf("Codegen: unary '%c'\n", op);
  Value *operandVal = operand->codegen();
  if (!operandVal) {
    return nullptr;
  }

  Function *func = getFunction(std::string("unary") + op);
  if (!func) {
    return logErrorV("Unknown unary operator");
  }

  return builder->CreateCall(func, operandVal, "unop");
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
    break;
  }

  Function *func = getFunction(std::string("binary") + op);
  if (!func) {
    return logErrorV("invalid binary operator");
  }

  Value *ops[] = {L, R};
  return builder->CreateCall(func, ops, "binop");
}

Value *VarExprAST::codegen() {
  devPrintf("Codegen: var\n");
  std::vector<AllocaInst *> oldBindings;

  Function *func = builder->GetInsertBlock()->getParent();

  for (auto &var : varNames) {
    const std::string &name = var.first;
    ExprAST *initExpr = var.second.get();

    Value *initVal;
    if (initExpr) {
      initVal = initExpr->codegen();
      if (!initVal) {
        return nullptr;
      }
    } else {
      initVal = ConstantFP::get(*theContext, APFloat(0.0));
    }

    AllocaInst *alloca = createEntryBlockAlloca(func, name);
    builder->CreateStore(initVal, alloca);

    oldBindings.push_back(namedValues[name]);
    namedValues[name] = alloca;
  }

  Value *bodyVal = body->codegen();
  if (!bodyVal) {
    return nullptr;
  }

  for (unsigned i = 0; i < varNames.size(); ++i) {
    const std::string &name = varNames[i].first;
    if (oldBindings[i]) {
      namedValues[name] = oldBindings[i];
    } else {
      namedValues.erase(name);
    }
  }

  return bodyVal;
}

Value *IfExprAST::codegen() {
  devPrintf("Codegen: if\n");
  // Convert condition to a boolean by comparing against 0.0
  Value *condVal = condExpr->codegen();
  if (!condVal) {
    return nullptr;
  }

  condVal = builder->CreateFCmpONE(
      condVal, ConstantFP::get(*theContext, APFloat(0.0)), "ifcond");

  Function *func = builder->GetInsertBlock()->getParent();

  // Build then/else/merge blocks and branch on the condition
  BasicBlock *thenBB = BasicBlock::Create(*theContext, "then", func);
  BasicBlock *elseBB = BasicBlock::Create(*theContext, "else", func);
  BasicBlock *mergeBB = BasicBlock::Create(*theContext, "ifcont", func);

  builder->CreateCondBr(condVal, thenBB, elseBB);

  builder->SetInsertPoint(thenBB);
  Value *thenVal = thenExpr->codegen();
  if (!thenVal) {
    return nullptr;
  }
  builder->CreateBr(mergeBB);
  thenBB = builder->GetInsertBlock();

  builder->SetInsertPoint(elseBB);
  Value *elseVal = elseExpr->codegen();
  if (!elseVal) {
    return nullptr;
  }
  builder->CreateBr(mergeBB);
  elseBB = builder->GetInsertBlock();

  builder->SetInsertPoint(mergeBB);
  // Merge the two control-flow paths with a PHI node
  PHINode *phi = builder->CreatePHI(Type::getDoubleTy(*theContext), 2, "iftmp");
  phi->addIncoming(thenVal, thenBB);
  phi->addIncoming(elseVal, elseBB);
  return phi;
}

Value *ForExprAST::codegen() {
  devPrintf("Codegen: for %s\n", varName.c_str());
  // Emit the loop variable initialization
  Value *startVal = startExpr->codegen();
  if (!startVal) {
    return nullptr;
  }

  Function *func = builder->GetInsertBlock()->getParent();
  AllocaInst *alloca = createEntryBlockAlloca(func, varName);
  builder->CreateStore(startVal, alloca);

  BasicBlock *loopBB = BasicBlock::Create(*theContext, "loop", func);

  builder->CreateBr(loopBB);
  builder->SetInsertPoint(loopBB);

  AllocaInst *oldVal = namedValues[varName];
  namedValues[varName] = alloca;

  if (!body->codegen()) {
    return nullptr;
  }

  // Compute the step or default to 1.0
  Value *stepVal = nullptr;
  if (stepExpr) {
    stepVal = stepExpr->codegen();
    if (!stepVal) {
      return nullptr;
    }
  } else {
    stepVal = ConstantFP::get(*theContext, APFloat(1.0));
  }

  Value *curVar =
      builder->CreateLoad(Type::getDoubleTy(*theContext), alloca, varName);
  Value *nextVar = builder->CreateFAdd(curVar, stepVal, "nextvar");
  builder->CreateStore(nextVar, alloca);

  // Evaluate the loop condition
  Value *endCond = endExpr->codegen();
  if (!endCond) {
    return nullptr;
  }

  endCond = builder->CreateFCmpONE(
      endCond, ConstantFP::get(*theContext, APFloat(0.0)), "loopcond");

  BasicBlock *afterBB = BasicBlock::Create(*theContext, "afterloop", func);
  builder->CreateCondBr(endCond, loopBB, afterBB);

  builder->SetInsertPoint(afterBB);

  // Restore any shadowed variable
  if (oldVal) {
    namedValues[varName] = oldVal;
  } else {
    namedValues.erase(varName);
  }

  return Constant::getNullValue(Type::getDoubleTy(*theContext));
}

Value *CallExprAST::codegen() {
  devPrintf("Codegen: call %s (%zu args)\n", callee.c_str(), args.size());
  // Look up name in global module table
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
    AllocaInst *alloca = createEntryBlockAlloca(func, arg.getName().str());
    builder->CreateStore(&arg, alloca);
    namedValues[arg.getName().str()] = alloca;
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
