#include "AbstractSyntaxTree.h"
#include "Debug.h"
#include "KaleidoscopeJIT.h"
#include "Lexer.h"
#include "Parser.h"

#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"

#include <cstdio>

namespace Compiler {

extern std::map<char, int> binopPrecedence;

static llvm::ExitOnError exitOnErr;

static std::unique_ptr<llvm::orc::KaleidoscopeJIT> theJIT;
static std::unique_ptr<llvm::FunctionPassManager> functionPassManager;
static std::unique_ptr<llvm::LoopAnalysisManager> loopAnalysisManager;
static std::unique_ptr<llvm::FunctionAnalysisManager> functionAnalysisManager;
static std::unique_ptr<llvm::CGSCCAnalysisManager> cgsccAnalysisManager;
static std::unique_ptr<llvm::ModuleAnalysisManager> moduleAnalysisManager;

void initializeModule();

void handleDefinition() {
  if (auto funcAST = parseDefinition()) {
    functionProtos[funcAST->getProto().getName()] = funcAST->getProto().clone();
    if (auto *funcIR = funcAST->codegen()) {
      devPrintf("Optimizing function: %s\n", funcIR->getName().str().c_str());
      functionPassManager->run(*funcIR, *functionAnalysisManager);
      devPrintIR("Read function definition: ", funcIR);
      exitOnErr(theJIT->addModule(llvm::orc::ThreadSafeModule(
          std::move(theModule), std::move(theContext))));
      initializeModule();
    }
  } else {
    // Skip token for error recovery
    getNextToken();
  }
}

void handleExtern() {
  if (auto protoAST = parseExtern()) {
    functionProtos[protoAST->getName()] = protoAST->clone();
    if (auto *funcIR = functionProtos[protoAST->getName()]->codegen()) {
      devPrintIR("Read extern: ", funcIR);
    }
  } else {
    // Skip token for error recovery
    getNextToken();
  }
}

void handleTopLevelExpression() {
  // Wrap in anonymous function
  if (auto funcAST = parseTopLevelExpr()) {
    if (auto *funcIR = funcAST->codegen()) {
      functionPassManager->run(*funcIR, *functionAnalysisManager);
      devPrintIR("Read top-level expression: \n", funcIR);

      auto resourceTracker = theJIT->getMainJITDylib().createResourceTracker();
      exitOnErr(
          theJIT->addModule(llvm::orc::ThreadSafeModule(std::move(theModule),
                                                        std::move(theContext)),
                            resourceTracker));
      initializeModule();

      auto exprSymbol = exitOnErr(theJIT->lookup("__anon_expr"));
      auto *fp = exprSymbol.getAddress().toPtr<double (*)()>();
      fprintf(stderr, "Evaluated to %f\n", fp());

      exitOnErr(resourceTracker->remove());
    }
  } else {
    // Skip token for error recovery
    getNextToken();
  }
}

void initializeModule() {
  theContext = std::make_unique<LLVMContext>();
  theModule = std::make_unique<Module>("Compiler", *theContext);
  theModule->setDataLayout(theJIT->getDataLayout());
  builder = std::make_unique<IRBuilder<>>(*theContext);

  functionPassManager = std::make_unique<llvm::FunctionPassManager>();
  functionPassManager->addPass(InstCombinePass());
  functionPassManager->addPass(ReassociatePass());
  functionPassManager->addPass(GVNPass());
  functionPassManager->addPass(SimplifyCFGPass());

  loopAnalysisManager = std::make_unique<llvm::LoopAnalysisManager>();
  functionAnalysisManager = std::make_unique<llvm::FunctionAnalysisManager>();
  cgsccAnalysisManager = std::make_unique<llvm::CGSCCAnalysisManager>();
  moduleAnalysisManager = std::make_unique<llvm::ModuleAnalysisManager>();

  llvm::PassBuilder passBuilder;
  passBuilder.registerModuleAnalyses(*moduleAnalysisManager);
  passBuilder.registerCGSCCAnalyses(*cgsccAnalysisManager);
  passBuilder.registerFunctionAnalyses(*functionAnalysisManager);
  passBuilder.registerLoopAnalyses(*loopAnalysisManager);
  passBuilder.crossRegisterProxies(
      *loopAnalysisManager, *functionAnalysisManager, *cgsccAnalysisManager,
      *moduleAnalysisManager);
}

void setup() {
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();
  theJIT = exitOnErr(llvm::orc::KaleidoscopeJIT::Create());

  // Install standard binary operators.
  // 1 is lowest precedence.
  binopPrecedence['<'] = 10;
  binopPrecedence['+'] = 20;
  binopPrecedence['-'] = 20;
  binopPrecedence['*'] = 40; // highest.

  // Prime the first token.
  fprintf(stderr, "ready> ");
  getNextToken();

  // Initialize
  initializeModule();
}

// top ::= definition | external | expression | ';'
void mainLoop() {
  setup();
  while (true) {
    switch (curTok) {
    case tok_eof:
      return;
    case ';':
      getNextToken();
      break;
    case tok_def:
      handleDefinition();
      break;
    case tok_extern:
      handleExtern();
      break;
    default:
      handleTopLevelExpression();
      break;
    }
    fprintf(stderr, "ready> ");
  }
}

} // namespace Compiler

int main(int argc, char **argv) {
  Compiler::initDevModeFromArgs(argc, argv);
  // Run the main "interpreter loop"
  Compiler::mainLoop();

  return 0;
}
