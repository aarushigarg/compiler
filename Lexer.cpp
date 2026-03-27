#include "Lexer.h"

#include "Debug.h"

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

// Dev-only token tracing to follow the lexer stream
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
  if (tok == tok_binary) {
    devPrintf("Lexer: tok_binary\n");
    return;
  }
  if (tok == tok_unary) {
    devPrintf("Lexer: tok_unary\n");
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
  if (tok == tok_if) {
    devPrintf("Lexer: tok_if\n");
    return;
  }
  if (tok == tok_then) {
    devPrintf("Lexer: tok_then\n");
    return;
  }
  if (tok == tok_else) {
    devPrintf("Lexer: tok_else\n");
    return;
  }
  if (tok == tok_for) {
    devPrintf("Lexer: tok_for\n");
    return;
  }
  if (tok == tok_in) {
    devPrintf("Lexer: tok_in\n");
    return;
  }
  if (tok == tok_var) {
    devPrintf("Lexer: tok_var\n");
    return;
  }
  if (tok == tok_sync) {
    devPrintf("Lexer: tok_sync\n");
    return;
  }
  if (tok > 0 && std::isprint(tok)) {
    devPrintf("Lexer: '%c'\n", tok);
    return;
  }
  devPrintf("Lexer: token %d\n", tok);
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
      devLogToken(tok_def);
      return tok_def;
    }
    if (identifierStr == "extern") {
      devLogToken(tok_extern);
      return tok_extern;
    }
    if (identifierStr == "binary") {
      devLogToken(tok_binary);
      return tok_binary;
    }
    if (identifierStr == "unary") {
      devLogToken(tok_unary);
      return tok_unary;
    }
    if (identifierStr == "if") {
      devLogToken(tok_if);
      return tok_if;
    }
    if (identifierStr == "then") {
      devLogToken(tok_then);
      return tok_then;
    }
    if (identifierStr == "else") {
      devLogToken(tok_else);
      return tok_else;
    }
    if (identifierStr == "for") {
      devLogToken(tok_for);
      return tok_for;
    }
    if (identifierStr == "in") {
      devLogToken(tok_in);
      return tok_in;
    }
    if (identifierStr == "var") {
      devLogToken(tok_var);
      return tok_var;
    }
    if (identifierStr == "sync") {
      devLogToken(tok_sync);
      return tok_sync;
    }
    devLogToken(tok_identifier);
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
    devLogToken(tok_number);
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
    devLogToken(tok_eof);
    return tok_eof;
  }

  // Otherwise return the ASCII value of the character
  int thisChar = lastChar;
  lastChar = advance();
  devLogToken(thisChar);
  return thisChar;
}

int getNextToken() { return curTok = gettok(); }

} // namespace Compiler
