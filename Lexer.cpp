#include "Lexer.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace Compiler {

int curTok;
std::string identifierStr;
double numVal;
SourceLocation curLoc = {1, 1};

static SourceLocation lexLoc = {1, 0};
static FILE *input = stdin;
static int lastChar = ' ';

static int advance() {
  int nextChar = getc(input);
  if (nextChar == '\n' || nextChar == '\r') {
    ++lexLoc.line;
    lexLoc.col = 0;
  } else if (nextChar != EOF) {
    ++lexLoc.col;
  }
  return nextChar;
}

void setInputFile(FILE *inputFile) {
  input = inputFile ? inputFile : stdin;
  resetLexerState();
}

void resetLexerState() {
  lexLoc = {1, 0};
  curLoc = {1, 1};
  identifierStr.clear();
  numVal = 0;
  curTok = 0;
  lastChar = ' ';
}

// Return the next token from the configured input stream
int gettok() {
  // Skip whitespace
  while (isspace(lastChar)) {
    lastChar = advance();
  }

  // The location of this token is the location of lastChar.
  curLoc = lexLoc;

  // Identifier or keyword
  if (isalpha(lastChar)) {
    // Starts with letter
    identifierStr = lastChar;
    // Rest is alphanumeric
    while (isalnum((lastChar = advance()))) {
      identifierStr += lastChar;
    }

    if (identifierStr == "def") {
      return tok_def;
    }
    if (identifierStr == "extern") {
      return tok_extern;
    }
    if (identifierStr == "binary") {
      return tok_binary;
    }
    if (identifierStr == "unary") {
      return tok_unary;
    }
    if (identifierStr == "if") {
      return tok_if;
    }
    if (identifierStr == "then") {
      return tok_then;
    }
    if (identifierStr == "else") {
      return tok_else;
    }
    if (identifierStr == "for") {
      return tok_for;
    }
    if (identifierStr == "in") {
      return tok_in;
    }
    if (identifierStr == "var") {
      return tok_var;
    }
    if (identifierStr == "sync") {
      return tok_sync;
    }
    if (identifierStr == "async") {
      return tok_async;
    }
    return tok_identifier;
  }

  // Number including decimal
  if (isdigit(lastChar) || lastChar == '.') {
    std::string numStr;
    do {
      numStr += lastChar;
      lastChar = advance();
    } while (isdigit(lastChar) || lastChar == '.');

    numVal = strtod(numStr.c_str(), nullptr);
    return tok_number;
  }

  // Comment
  if (lastChar == '#') {
    do {
      lastChar = advance();
    } while (lastChar != EOF && lastChar != '\n' && lastChar != '\r');

    if (lastChar != EOF) {
      // Get next token if exists
      return gettok();
    }
  }

  if (lastChar == EOF) {
    return tok_eof;
  }

  // Otherwise return the ASCII value of the character
  int thisChar = lastChar;
  lastChar = advance();
  return thisChar;
}

int getNextToken() { return curTok = gettok(); }

} // namespace Compiler
