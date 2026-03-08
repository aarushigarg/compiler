#include "AbstractSyntaxTree.h"
#include "Debug.h"
#include "Lexer.h"
#include "Parser.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

#include <cstdio>
#include <optional>
#include <system_error>

namespace Compiler {

extern std::map<char, int> binopPrecedence;

void initializeModule();
bool emitObjectFile(const std::string &filename);

void handleDefinition() {
  if (auto funcAST = parseDefinition()) {
    functionProtos[funcAST->getProto().getName()] = funcAST->getProto().clone();
    if (auto *funcIR = funcAST->codegen()) {
      devPrintIR("Read function definition: ", funcIR);
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
  // Parse and codegen anonymous top-level expression.
  if (auto funcAST = parseTopLevelExpr()) {
    if (auto *funcIR = funcAST->codegen()) {
      devPrintIR("Read top-level expression: ", funcIR);
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
  // Install standard binary operators
  // 1 is lowest precedence
  binopPrecedence['<'] = 10;
  binopPrecedence['+'] = 20;
  binopPrecedence['-'] = 20;
  binopPrecedence['*'] = 40; // highest

  // Prime the first token
  fprintf(stderr, "ready> ");
  getNextToken();

  // Initialize
  initializeModule();
}

bool emitObjectFile(const std::string &filename) {
  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  std::string targetTripleStr = llvm::sys::getDefaultTargetTriple();
  llvm::Triple targetTriple(targetTripleStr);
  theModule->setTargetTriple(targetTriple);

  std::string error;
  auto *target = llvm::TargetRegistry::lookupTarget(targetTriple, error);
  if (!target) {
    llvm::errs() << error << '\n';
    return false;
  }

  auto CPU = "generic";
  auto features = "";

  llvm::TargetOptions options;
  auto relocationModel = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
  std::unique_ptr<llvm::TargetMachine> targetMachine(
      target->createTargetMachine(targetTriple, CPU, features, options,
                                  relocationModel));

  theModule->setDataLayout(targetMachine->createDataLayout());

  std::error_code errorCode;
  llvm::raw_fd_ostream dest(filename, errorCode, llvm::sys::fs::OF_None);
  if (errorCode) {
    llvm::errs() << "Could not open file: " << errorCode.message() << '\n';
    return false;
  }

  llvm::legacy::PassManager pass;
  auto fileType = llvm::CodeGenFileType::ObjectFile;
  if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
    llvm::errs() << "TheTargetMachine can't emit a file of this type\n";
    return false;
  }

  pass.run(*theModule);
  dest.flush();
  llvm::outs() << "Wrote " << filename << '\n';
  return true;
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
    // Prompt after each completed action
    fprintf(stderr, "ready> ");
  }
}

} // namespace Compiler

int main(int argc, char **argv) {
  Compiler::initDevModeFromArgs(argc, argv);
  // Run the main "interpreter loop"
  Compiler::mainLoop();
  if (!Compiler::emitObjectFile("output.o")) {
    return 1;
  }

  return 0;
}
