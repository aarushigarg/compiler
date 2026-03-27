#include "AbstractSyntaxTree.h"

#include "LogErrors.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
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

namespace {

class DebugInfo {
public:
  std::unique_ptr<DIBuilder> diBuilder;
  DICompileUnit *compileUnit = nullptr;
  DIFile *unit = nullptr;
  DIType *doubleType = nullptr;
  std::vector<DIScope *> lexicalBlocks;

  void initialize(Module &module, const std::string &sourceName) {
    diBuilder = std::make_unique<DIBuilder>(module);
    unit = diBuilder->createFile(sourceName, ".");
    compileUnit = diBuilder->createCompileUnit(dwarf::DW_LANG_C, unit,
                                               "Compiler", false, "", 0);
    doubleType = nullptr;
    lexicalBlocks.clear();
  }

  void finalize() {
    if (diBuilder) {
      diBuilder->finalize();
    }
  }

  DIType *getDoubleType() {
    if (!doubleType) {
      doubleType =
          diBuilder->createBasicType("double", 64, dwarf::DW_ATE_float);
    }
    return doubleType;
  }

  DISubroutineType *createFunctionType(unsigned argCount) {
    std::vector<Metadata *> types;
    types.push_back(getDoubleType());
    for (unsigned i = 0; i < argCount; ++i) {
      types.push_back(getDoubleType());
    }
    return diBuilder->createSubroutineType(
        diBuilder->getOrCreateTypeArray(types));
  }

  DIScope *currentScope() const {
    if (!lexicalBlocks.empty()) {
      return lexicalBlocks.back();
    }
    return unit;
  }

  void emitLocation(const ExprAST *expr) {
    if (!expr) {
      Compiler::builder->SetCurrentDebugLocation(DebugLoc());
      return;
    }

    SourceLocation loc = expr->getLoc();
    if (loc.line <= 0 || loc.col <= 0) {
      return;
    }

    IRBuilderBase::InsertPoint insertPt = Compiler::builder->saveIP();
    if (!insertPt.getBlock()) {
      return;
    }

    Compiler::builder->SetCurrentDebugLocation(DILocation::get(
        insertPt.getBlock()->getContext(), loc.line, loc.col, currentScope()));
  }
};

DebugInfo debugInfo;
std::size_t asyncWrapperCounter = 0;

} // namespace

void initializeDebugInfo(const std::string &sourceName) {
  debugInfo.initialize(*theModule, sourceName);
}

void finalizeDebugInfo() { debugInfo.finalize(); }

std::string PrototypeAST::getSymbolName() const {
  if (name == "main" && args.empty()) {
    return "__program_main";
  }
  return name;
}

static Function *getFunction(const std::string &name) {
  if (name == "main") {
    if (auto *func = theModule->getFunction("__program_main")) {
      return func;
    }
  }

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
  IRBuilder<> tmpBuilder(&func->getEntryBlock(), func->getEntryBlock().begin());
  return tmpBuilder.CreateAlloca(Type::getDoubleTy(*theContext), nullptr,
                                 varName.c_str());
}

static Function *getOrCreateRuntimeFunction(const std::string &name,
                                            FunctionType *funcType) {
  if (auto *func = theModule->getFunction(name)) {
    if (func->getFunctionType() == funcType) {
      return func;
    }
    return nullptr;
  }

  return Function::Create(funcType, Function::ExternalLinkage, name,
                          theModule.get());
}

static Function *getOrCreateMallocFunction() {
  FunctionType *mallocType = FunctionType::get(
      PointerType::get(*theContext, 0), {Type::getInt64Ty(*theContext)}, false);
  return getOrCreateRuntimeFunction("malloc", mallocType);
}

static Function *getOrCreateFreeFunction() {
  FunctionType *freeType = FunctionType::get(
      Type::getVoidTy(*theContext), {PointerType::get(*theContext, 0)}, false);
  return getOrCreateRuntimeFunction("free", freeType);
}

static Function *createAsyncWrapper(Function *calleeF, std::size_t argCount) {
  PointerType *ptrTy = PointerType::get(*theContext, 0);
  Type *doubleTy = Type::getDoubleTy(*theContext);
  // Each wrapper has the generic runtime shape: void wrapper(void *data).
  FunctionType *wrapperType =
      FunctionType::get(Type::getVoidTy(*theContext), {ptrTy}, false);
  std::string wrapperName =
      "__compiler_async_wrapper_" + std::to_string(asyncWrapperCounter++);
  Function *wrapperFunc = Function::Create(
      wrapperType, Function::PrivateLinkage, wrapperName, theModule.get());

  BasicBlock *entryBB = BasicBlock::Create(*theContext, "entry", wrapperFunc);
  IRBuilder<> wrapperBuilder(entryBB);

  Argument *rawData = wrapperFunc->getArg(0);
  rawData->setName("rawdata");
  // The payload is stored as a heap array of doubles.
  Value *doubleData = wrapperBuilder.CreateBitCast(rawData, ptrTy, "args");

  std::vector<Value *> callArgs;
  callArgs.reserve(argCount);
  for (std::size_t i = 0; i < argCount; ++i) {
    // Load each argument back out of the payload in order.
    Value *index = ConstantInt::get(Type::getInt64Ty(*theContext),
                                    static_cast<uint64_t>(i));
    Value *argPtr =
        wrapperBuilder.CreateInBoundsGEP(doubleTy, doubleData, index, "argptr");
    callArgs.push_back(
        wrapperBuilder.CreateLoad(doubleTy, argPtr, "arg" + std::to_string(i)));
  }

  // Call the original function, then free the heap payload.
  wrapperBuilder.CreateCall(calleeF, callArgs);
  Function *freeFunc = getOrCreateFreeFunction();
  wrapperBuilder.CreateCall(freeFunc, rawData);
  wrapperBuilder.CreateRetVoid();
  verifyFunction(*wrapperFunc);
  return wrapperFunc;
}

Value *NumberExprAST::codegen() {
  debugInfo.emitLocation(this);
  return ConstantFP::get(*theContext, APFloat(val));
}

Value *VariableExprAST::codegen() {
  debugInfo.emitLocation(this);
  // Look up variable name
  AllocaInst *V = namedValues[name];
  if (!V) {
    return logErrorV(("Unknown variable name: " + name).c_str());
  }
  return builder->CreateLoad(Type::getDoubleTy(*theContext), V, name.c_str());
}

Value *UnaryExprAST::codegen() {
  debugInfo.emitLocation(this);
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
  debugInfo.emitLocation(this);
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
  debugInfo.emitLocation(this);
  std::vector<AllocaInst *> oldBindings;

  Function *func = builder->GetInsertBlock()->getParent();
  DILexicalBlock *scopeBlock = debugInfo.diBuilder->createLexicalBlock(
      debugInfo.currentScope(), debugInfo.unit, getLoc().line, getLoc().col);
  debugInfo.lexicalBlocks.push_back(scopeBlock);

  for (auto &var : varNames) {
    const std::string &name = var.first;
    ExprAST *initExpr = var.second.get();

    Value *initVal;
    if (initExpr) {
      initVal = initExpr->codegen();
      if (!initVal) {
        debugInfo.lexicalBlocks.pop_back();
        return nullptr;
      }
    } else {
      initVal = ConstantFP::get(*theContext, APFloat(0.0));
    }

    AllocaInst *alloca = createEntryBlockAlloca(func, name);
    builder->CreateStore(initVal, alloca);

    DILocalVariable *debugVar = debugInfo.diBuilder->createAutoVariable(
        scopeBlock, name, debugInfo.unit, getLoc().line,
        debugInfo.getDoubleType());
    debugInfo.diBuilder->insertDeclare(
        alloca, debugVar, debugInfo.diBuilder->createExpression(),
        DILocation::get(*theContext, getLoc().line, getLoc().col, scopeBlock),
        builder->GetInsertBlock());

    oldBindings.push_back(namedValues[name]);
    namedValues[name] = alloca;
  }

  Value *bodyVal = body->codegen();

  for (unsigned i = 0; i < varNames.size(); ++i) {
    const std::string &name = varNames[i].first;
    if (oldBindings[i]) {
      namedValues[name] = oldBindings[i];
    } else {
      namedValues.erase(name);
    }
  }

  debugInfo.lexicalBlocks.pop_back();
  if (!bodyVal) {
    return nullptr;
  }

  return bodyVal;
}

Value *IfExprAST::codegen() {
  debugInfo.emitLocation(this);
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
  debugInfo.emitLocation(this);
  // Emit the loop variable initialization
  Value *startVal = startExpr->codegen();
  if (!startVal) {
    return nullptr;
  }

  Function *func = builder->GetInsertBlock()->getParent();
  AllocaInst *alloca = createEntryBlockAlloca(func, varName);
  builder->CreateStore(startVal, alloca);

  DILexicalBlock *scopeBlock = debugInfo.diBuilder->createLexicalBlock(
      debugInfo.currentScope(), debugInfo.unit, getLoc().line, getLoc().col);
  debugInfo.lexicalBlocks.push_back(scopeBlock);
  DILocalVariable *debugVar = debugInfo.diBuilder->createAutoVariable(
      scopeBlock, varName, debugInfo.unit, getLoc().line,
      debugInfo.getDoubleType());
  debugInfo.diBuilder->insertDeclare(
      alloca, debugVar, debugInfo.diBuilder->createExpression(),
      DILocation::get(*theContext, getLoc().line, getLoc().col, scopeBlock),
      builder->GetInsertBlock());

  BasicBlock *loopBB = BasicBlock::Create(*theContext, "loop", func);

  builder->CreateBr(loopBB);
  builder->SetInsertPoint(loopBB);

  AllocaInst *oldVal = namedValues[varName];
  namedValues[varName] = alloca;

  if (!body->codegen()) {
    debugInfo.lexicalBlocks.pop_back();
    return nullptr;
  }

  // Compute the step or default to 1.0
  Value *stepVal = nullptr;
  if (stepExpr) {
    stepVal = stepExpr->codegen();
    if (!stepVal) {
      debugInfo.lexicalBlocks.pop_back();
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
    debugInfo.lexicalBlocks.pop_back();
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
  debugInfo.lexicalBlocks.pop_back();

  return Constant::getNullValue(Type::getDoubleTy(*theContext));
}

Value *CallExprAST::codegen() {
  debugInfo.emitLocation(this);
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

Value *SyncExprAST::codegen() {
  debugInfo.emitLocation(this);

  FunctionType *syncType =
      FunctionType::get(Type::getDoubleTy(*theContext), false);
  Function *syncFunc =
      getOrCreateRuntimeFunction("__compiler_sync_tasks", syncType);
  if (!syncFunc) {
    return logErrorV(
        "Runtime function signature mismatch: __compiler_sync_tasks");
  }

  return builder->CreateCall(syncFunc, {}, "synctmp");
}

Value *AsyncExprAST::codegen() {
  debugInfo.emitLocation(this);

  // The async payload lives past the current function, so it must be heap
  // allocated instead of using an alloca in the caller's stack frame.
  Function *mallocFunc = getOrCreateMallocFunction();
  if (!mallocFunc) {
    return logErrorV("Could not declare malloc");
  }

  // Resolve the function being scheduled.
  Function *calleeF = getFunction(callee);
  if (!calleeF) {
    return logErrorV(
        ("Unknown function referenced in async: " + callee).c_str());
  }
  if (calleeF->arg_size() != args.size()) {
    return logErrorV("Incorrect number of arguments passed to async");
  }

  // Evaluate async arguments and store their values in a payload.
  std::vector<Value *> argValues;
  argValues.reserve(args.size());
  for (auto &arg : args) {
    Value *argVal = arg->codegen();
    if (!argVal) {
      return nullptr;
    }
    argValues.push_back(argVal);
  }

  Type *doubleTy = Type::getDoubleTy(*theContext);
  PointerType *ptrTy = PointerType::get(*theContext, 0);

  // No-argument async calls can use a null payload.
  Value *rawData = ConstantPointerNull::get(ptrTy);
  if (!argValues.empty()) {
    uint64_t payloadBytes = static_cast<uint64_t>(argValues.size()) *
                            (doubleTy->getPrimitiveSizeInBits() / 8);
    Value *allocSize =
        ConstantInt::get(Type::getInt64Ty(*theContext), payloadBytes);
    rawData = builder->CreateCall(mallocFunc, {allocSize}, "asyncdata");
    Value *doubleData = builder->CreateBitCast(rawData, ptrTy, "doubledata");

    for (std::size_t i = 0; i < argValues.size(); ++i) {
      // Write each argument into the heap payload in call order.
      Value *index = ConstantInt::get(Type::getInt64Ty(*theContext),
                                      static_cast<uint64_t>(i));
      Value *argPtr =
          builder->CreateInBoundsGEP(doubleTy, doubleData, index, "argptr");
      builder->CreateStore(argValues[i], argPtr);
    }
  }

  // Build a wrapper that knows how to unpack the payload and call calleeF.
  Function *wrapperFunc = createAsyncWrapper(calleeF, argValues.size());

  // Hand the wrapper and payload pointer off to the runtime entry
  // point, which will queue them on the worker pool.
  FunctionType *helperType = FunctionType::get(
      Type::getDoubleTy(*theContext), {wrapperFunc->getType(), ptrTy}, false);
  Function *helperFunc =
      getOrCreateRuntimeFunction("__compiler_async_call", helperType);
  if (!helperFunc) {
    return logErrorV(
        "Runtime function signature mismatch: __compiler_async_call");
  }

  return builder->CreateCall(helperFunc, {wrapperFunc, rawData}, "asynctmp");
}

Function *PrototypeAST::codegen() {
  // Function with return type double and arguments of type double
  std::vector<Type *> Doubles(args.size(), Type::getDoubleTy(*theContext));
  FunctionType *funcType =
      FunctionType::get(Type::getDoubleTy(*theContext), Doubles, false);
  std::string symbolName = getSymbolName();

  // Ensure existing function has matching signature
  Function *func;
  if ((func = theModule->getFunction(symbolName))) {
    if (func->getFunctionType() != funcType) {
      logErrorP("Function signature mismatch");
      return nullptr;
    }
  } else {
    func = Function::Create(funcType, Function::ExternalLinkage, symbolName,
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
  // First check for existing function from previous 'extern' declaration
  Function *func = theModule->getFunction(prototype->getSymbolName());

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

  SourceLocation protoLoc = prototype->getLoc();
  DISubprogram *subprogram = debugInfo.diBuilder->createFunction(
      debugInfo.unit, prototype->getName(), StringRef(), debugInfo.unit,
      protoLoc.line, debugInfo.createFunctionType(func->arg_size()),
      protoLoc.line, DINode::FlagPrototyped, DISubprogram::SPFlagDefinition);
  func->setSubprogram(subprogram);
  debugInfo.lexicalBlocks.push_back(subprogram);

  // Create a new basic block to start insertion into
  BasicBlock *basicBlock = BasicBlock::Create(*theContext, "entry", func);
  builder->SetInsertPoint(basicBlock);
  debugInfo.emitLocation(nullptr);

  // Record the function arguments in the named values map
  namedValues.clear();
  unsigned argNo = 0;
  for (auto &arg : func->args()) {
    ++argNo;
    AllocaInst *alloca = createEntryBlockAlloca(func, arg.getName().str());
    builder->CreateStore(&arg, alloca);
    namedValues[arg.getName().str()] = alloca;

    DILocalVariable *debugArg = debugInfo.diBuilder->createParameterVariable(
        subprogram, arg.getName(), argNo, debugInfo.unit, protoLoc.line,
        debugInfo.getDoubleType(), true);
    debugInfo.diBuilder->insertDeclare(
        alloca, debugArg, debugInfo.diBuilder->createExpression(),
        DILocation::get(*theContext, protoLoc.line, 0, subprogram), basicBlock);
  }

  debugInfo.emitLocation(body.get());
  if (Value *retVal = body->codegen()) {
    // Finish the function by creating ret
    builder->CreateRet(retVal);

    // Validate the generated function
    verifyFunction(*func);
    debugInfo.lexicalBlocks.pop_back();

    return func;
  }

  // Error reading body, remove function
  debugInfo.lexicalBlocks.pop_back();
  func->eraseFromParent();
  return nullptr;
}

} // namespace Compiler
