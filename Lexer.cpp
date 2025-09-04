#include "Lexer.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace Compiler {

int curTok;
std::string identifierStr;
double numVal;

// Return next token from standard input
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

    if (identifierStr == "def")
      return tok_def;
    if (identifierStr == "extern")
      return tok_extern;
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
    return tok_eof;
  }

  // Else, return character as its ASCII value
  int thisChar = lastChar;
  lastChar = getchar();
  return thisChar;
}

int getNextToken() { return curTok = gettok(); }

} // namespace Compiler