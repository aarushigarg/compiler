#include "AbstractSyntaxTree.h"
#include "Lexer.h"
#include "Parser.h"

#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"

#include <cstdio>
#include <map>

namespace Compiler {

extern std::map<char, int> binopPrecedence;

void handleDefinition() {
  if (auto funcAST = parseDefinition()) {
    if (auto *funcIR = funcAST->codegen()) {
      fprintf(stderr, "Read function definition: ");
      funcIR->print(errs());
      fprintf(stderr, "\n");
    }
  } else {
    // Skip token for error recovery
    getNextToken();
  }
}

void handleExtern() {
  if (auto protoAST = parseExtern()) {
    if (auto *funcIR = protoAST->codegen()) {
      fprintf(stderr, "Read extern: ");
      funcIR->print(errs());
      fprintf(stderr, "\n");
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
      fprintf(stderr, "Read top-level expression: ");
      funcIR->print(errs());
      fprintf(stderr, "\n");

      // Remove the anonymous expression
      funcIR->eraseFromParent();
    }
  } else {
    // Skip token for error recovery
    getNextToken();
  }
}

void initializeModule() {
  theContext = std::make_unique<LLVMContext>();
  theModule = std::make_unique<Module>("Compiler", *theContext);
  builder = std::make_unique<IRBuilder<>>(*theContext);
}

void setup() {
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
    fprintf(stderr, "ready> ");
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
  }
}

} // namespace Compiler

int main() {
  // Run the main "interpreter loop"
  Compiler::mainLoop();

  Compiler::theModule->print(llvm::errs(), nullptr);
  return 0;
}