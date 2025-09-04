#pragma once

#include <string>

namespace Compiler {

// Lexer returns tokens (ASCII value) [0-255] for unknown characters
// and tokens < 0 for known keywords and identifiers
enum token {
  tok_eof = -1,

  // commands
  tok_def = -2,
  tok_extern = -3,

  // primary
  tok_identifier = -4,
  tok_number = -5,
};

extern int curTok;                // Current token
extern std::string identifierStr; // Contains value if tok_identifier
extern double numVal;             // Contains value if tok_number

int gettok();
int getNextToken();

} // namespace Compiler