#include "AbstractSyntaxTree.h"
#include "Lexer.h"
#include "LogErrors.h"
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
  FILE *stream = nullptr;
  std::string sourceName;
  std::string outputName;
};

struct CompileStatus {
  bool hasMain = false;
};

void initializeModule(const std::string &sourceName);
bool emitObjectFile(const std::string &filename);

std::string makeOutputFilename(const std::string &sourceName) {
  std::size_t lastSlash = sourceName.find_last_of("/\\");
  std::size_t lastDot = sourceName.find_last_of('.');
  if (lastDot == std::string::npos ||
      (lastSlash != std::string::npos && lastDot < lastSlash)) {
    return sourceName + ".o";
  }
  return sourceName.substr(0, lastDot) + ".o";
}

void handleExtern() {
  if (auto protoAST = parseExtern()) {
    functionProtos[protoAST->getName()] = protoAST->clone();
    functionProtos[protoAST->getName()]->codegen();
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

// top ::= definition | external
void mainLoop(const InputConfig &config, CompileStatus &status) {
  setup(config);
  while (true) {
    switch (curTok) {
    case tok_eof:
      return;
    case tok_def:
      if (auto funcAST = parseDefinition()) {
        const PrototypeAST &proto = funcAST->getProto();
        if (proto.getName() == "main") {
          if (!proto.getArgs().empty()) {
            logError("program entrypoint must be defined as def main()");
          } else {
            status.hasMain = true;
          }
        }
        if (!hadError) {
          functionProtos[proto.getName()] = proto.clone();
          funcAST->codegen();
        }
      } else {
        // Skip token for error recovery
        getNextToken();
      }
      break;
    case tok_extern:
      handleExtern();
      break;
    default:
      logError("top-level expressions are not allowed; wrap code in a function");
      return;
    }
  }
}

InputConfig parseInputConfig(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <source-file>\n", argv[0]);
    std::exit(1);
  }

  InputConfig config;
  const char *path = argv[1];
  FILE *file = fopen(path, "r");
  if (!file) {
    perror(path);
    std::exit(1);
  }

  config.stream = file;
  config.sourceName = path;
  config.outputName = makeOutputFilename(path);
  return config;
}

} // namespace Compiler

int main(int argc, char **argv) {
  Compiler::InputConfig inputConfig = Compiler::parseInputConfig(argc, argv);
  Compiler::CompileStatus compileStatus;
  Compiler::hadError = false;
  Compiler::mainLoop(inputConfig, compileStatus);
  if (inputConfig.stream) {
    fclose(inputConfig.stream);
  }
  if (Compiler::hadError) {
    return 1;
  }
  if (!Compiler::emitObjectFile(inputConfig.outputName)) {
    return 1;
  }
  llvm::outs() << "Entrypoint: "
               << (compileStatus.hasMain ? "main found" : "no main function")
               << '\n';

  return 0;
}
