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
  tok_binary = -4,
  tok_unary = -5,

  // primary
  tok_identifier = -6,
  tok_number = -7,

  // control
  tok_if = -8,
  tok_then = -9,
  tok_else = -10,
  tok_for = -11,
  tok_in = -12,
  tok_var = -13,
};

extern int curTok;                // Current token
extern std::string identifierStr; // Contains value if tok_identifier
extern double numVal;             // Contains value if tok_number

int gettok();
int getNextToken();

} // namespace Compiler
