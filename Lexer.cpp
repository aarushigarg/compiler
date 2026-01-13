#include "Lexer.h"

#include "Debug.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace Compiler {

int curTok;
std::string identifierStr;
double numVal;

// Return next token from standard input
static void devLogToken(int tok) {
  if (!isDevMode()) {
    return;
  }
  if (tok == tok_eof) {
    devPrintf("Lexer: tok_eof\n");
    return;
  }
  if (tok == tok_def) {
    devPrintf("Lexer: tok_def\n");
    return;
  }
  if (tok == tok_extern) {
    devPrintf("Lexer: tok_extern\n");
    return;
  }
  if (tok == tok_identifier) {
    devPrintf("Lexer: tok_identifier '%s'\n", identifierStr.c_str());
    return;
  }
  if (tok == tok_number) {
    devPrintf("Lexer: tok_number %f\n", numVal);
    return;
  }
  if (tok > 0 && std::isprint(tok)) {
    devPrintf("Lexer: '%c'\n", tok);
    return;
  }
  devPrintf("Lexer: token %d\n", tok);
}

int gettok() {
  static int lastChar = ' ';

  // Skip whitespace
  while (isspace(lastChar)) {
    lastChar = getchar();
  }

  // Identifier
  if (isalpha(lastChar)) {
    // Starts with letter
    identifierStr = lastChar;
    // Rest is alphanumeric
    while (isalnum((lastChar = getchar()))) {
      identifierStr += lastChar;
    }

    if (identifierStr == "def") {
      devLogToken(tok_def);
      return tok_def;
    }
    if (identifierStr == "extern") {
      devLogToken(tok_extern);
      return tok_extern;
    }
    devLogToken(tok_identifier);
    return tok_identifier;
  }

  // Number including decimal
  if (isdigit(lastChar) || lastChar == '.') {
    std::string numStr;
    do {
      numStr += lastChar;
      lastChar = getchar();
    } while (isdigit(lastChar) || lastChar == '.');

    numVal = strtod(numStr.c_str(), nullptr);
    devLogToken(tok_number);
    return tok_number;
  }

  // Comment
  if (lastChar == '#') {
    do {
      lastChar = getchar();
    } while (lastChar != EOF && lastChar != '\n' && lastChar != '\r');

    if (lastChar != EOF) {
      // Get next token if exists
      return gettok();
    }
  }

  if (lastChar == EOF) {
    devLogToken(tok_eof);
    return tok_eof;
  }

  // Else, return character as its ASCII value
  int thisChar = lastChar;
  lastChar = getchar();
  devLogToken(thisChar);
  return thisChar;
}

int getNextToken() { return curTok = gettok(); }

} // namespace Compiler
