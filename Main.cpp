#include "AbstractSyntaxTree.h"
#include "Debug.h"
#include "Lexer.h"
#include "Parser.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Metadata.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <system_error>

namespace Compiler {

extern std::map<char, int> binopPrecedence;

struct InputConfig {
  FILE *stream = stdin;
  std::string sourceName = "stdin";
  bool interactive = true;
};

void initializeModule(const std::string &sourceName);
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

void initializeModule(const std::string &sourceName) {
  theContext = std::make_unique<LLVMContext>();
  theModule = std::make_unique<Module>("Compiler", *theContext);
  theModule->addModuleFlag(llvm::Module::Warning, "Debug Info Version",
                           llvm::DEBUG_METADATA_VERSION);
  if (llvm::Triple(llvm::sys::getProcessTriple()).isOSDarwin()) {
    theModule->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 2);
  }
  builder = std::make_unique<IRBuilder<>>(*theContext);
  initializeDebugInfo(sourceName);
}

void setup(const InputConfig &config) {
  // Install standard binary operators
  // 1 is lowest precedence
  binopPrecedence['<'] = 10;
  binopPrecedence['+'] = 20;
  binopPrecedence['-'] = 20;
  binopPrecedence['*'] = 40; // highest

  setInputFile(config.stream);

  // Initialize
  initializeModule(config.sourceName);

  // Prime the first token
  if (config.interactive) {
    fprintf(stderr, "ready> ");
  }
  getNextToken();
}

bool emitObjectFile(const std::string &filename) {
  finalizeDebugInfo();
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

// top ::= definition | external | expression
void mainLoop(const InputConfig &config) {
  setup(config);
  while (true) {
    switch (curTok) {
    case tok_eof:
      return;
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
    if (config.interactive) {
      fprintf(stderr, "ready> ");
    }
  }
}

InputConfig parseInputConfig(int argc, char **argv) {
  InputConfig config;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--dev" || arg == "-d") {
      continue;
    }
    if (arg == "--stdin") {
      config.stream = stdin;
      config.sourceName = "stdin";
      config.interactive = true;
      continue;
    }
    if (arg == "--file") {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: missing path after --file\n");
        std::exit(1);
      }
      const char *path = argv[++i];
      FILE *file = fopen(path, "r");
      if (!file) {
        perror(path);
        std::exit(1);
      }
      config.stream = file;
      config.sourceName = path;
      config.interactive = false;
      continue;
    }

    fprintf(stderr, "Error: unknown argument '%s'\n", arg.c_str());
    std::exit(1);
  }

  return config;
}

} // namespace Compiler

int main(int argc, char **argv) {
  Compiler::initDevModeFromArgs(argc, argv);
  Compiler::InputConfig inputConfig = Compiler::parseInputConfig(argc, argv);
  // Run the main "interpreter loop"
  Compiler::mainLoop(inputConfig);
  if (inputConfig.stream != stdin) {
    fclose(inputConfig.stream);
  }
  if (!Compiler::emitObjectFile("output.o")) {
    return 1;
  }

  return 0;
}
