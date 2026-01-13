#include "Parser.h"

#include "AbstractSyntaxTree.h"
#include "Debug.h"
#include "Lexer.h"
#include "LogErrors.h"

#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Compiler {

// Holds precedence of defined binary operators
std::map<char, int> binopPrecedence;

// Get precedence of pending (current) binary operator token
int getTokPrecedence() {
  if (!isascii(curTok)) {
    return -1; // Not a binary operator
  }

  int tokPrecedence = binopPrecedence[curTok];
  if (tokPrecedence <= 0) {
    return -1; // Not a defined operator
  }
  return tokPrecedence;
}

// std::unique_ptr<ExprAST> parseExpression();

// numberexpr ::= number
std::unique_ptr<ExprAST> parseNumberExpr() {
  devPrintf("Parser: parseNumberExpr\n");
  auto result = std::make_unique<NumberExprAST>(numVal);
  getNextToken();
  return std::move(result);
}

// parenexpr ::= '(' expression ')'
std::unique_ptr<ExprAST> parseParenExpr() {
  devPrintf("Parser: parseParenExpr\n");
  getNextToken(); // eat '('
  auto expr = parseExpression();
  if (!expr) {
    return nullptr;
  }

  if (curTok != ')') {
    return logError("expected ')'");
  }
  getNextToken(); // eat ')'
  return expr;
}

// identifierexpr
//  ::= identifier
//  ::= identifier '(' expression* ')'
std::unique_ptr<ExprAST> parseIdentifierExpr() {
  devPrintf("Parser: parseIdentifierExpr\n");
  std::string idName = identifierStr;
  getNextToken(); // eat identifier

  if (curTok != '(') {
    // Simple variable ref
    return std::make_unique<VariableExprAST>(idName);
  }

  // Function call
  getNextToken(); // eat '('
  std::vector<std::unique_ptr<ExprAST>> args;

  if (curTok != ')') {
    while (true) {
      auto arg = parseExpression();
      if (!arg) {
        return nullptr;
      }
      args.push_back(std::move(arg));

      if (curTok == ')') {
        break;
      }

      if (curTok != ',') {
        return logError("expected ')' or ',' in argument list");
      }
      getNextToken();
    }
  }
  getNextToken(); // eat ')'

  return std::make_unique<CallExprAST>(idName, std::move(args));
}

// ifexpr ::= 'if' expression 'then' expression 'else' expression
std::unique_ptr<ExprAST> parseIfExpr() {
  devPrintf("Parser: parseIfExpr\n");
  // Parse conditional and both branches
  getNextToken(); // eat if

  auto condExpr = parseExpression();
  if (!condExpr) {
    return nullptr;
  }

  if (curTok != tok_then) {
    return logError("expected then");
  }
  getNextToken(); // eat then

  auto thenExpr = parseExpression();
  if (!thenExpr) {
    return nullptr;
  }

  if (curTok != tok_else) {
    return logError("expected else");
  }
  getNextToken(); // eat else

  auto elseExpr = parseExpression();
  if (!elseExpr) {
    return nullptr;
  }

  return std::make_unique<IfExprAST>(std::move(condExpr),
                                     std::move(thenExpr),
                                     std::move(elseExpr));
}

// forexpr ::= 'for' identifier '=' expr ',' expr (',' expr)? 'in' expression
std::unique_ptr<ExprAST> parseForExpr() {
  devPrintf("Parser: parseForExpr\n");
  getNextToken(); // eat for

  if (curTok != tok_identifier) {
    return logError("expected identifier after for");
  }

  std::string idName = identifierStr;
  getNextToken(); // eat identifier

  if (curTok != '=') {
    return logError("expected '=' after for");
  }
  getNextToken(); // eat '='

  auto startExpr = parseExpression();
  if (!startExpr) {
    return nullptr;
  }

  if (curTok != ',') {
    return logError("expected ',' after for start value");
  }
  getNextToken();

  auto endExpr = parseExpression();
  if (!endExpr) {
    return nullptr;
  }

  // Optional step expression
  std::unique_ptr<ExprAST> stepExpr;
  if (curTok == ',') {
    getNextToken();
    stepExpr = parseExpression();
    if (!stepExpr) {
      return nullptr;
    }
  }

  if (curTok != tok_in) {
    return logError("expected 'in' after for");
  }
  getNextToken(); // eat in

  // Loop body
  auto body = parseExpression();
  if (!body) {
    return nullptr;
  }

  return std::make_unique<ForExprAST>(idName, std::move(startExpr),
                                      std::move(endExpr), std::move(stepExpr),
                                      std::move(body));
}

// primary
//  ::= identifierexpr
//  ::= numberexpr
//  ::= parenexpr
//  ::= ifexpr
//  ::= forexpr
std::unique_ptr<ExprAST> parsePrimary() {
  devPrintf("Parser: parsePrimary\n");
  switch (curTok) {
  case tok_identifier:
    return parseIdentifierExpr();
  case tok_number:
    return parseNumberExpr();
  case '(':
    return parseParenExpr();
  case tok_if:
    return parseIfExpr();
  case tok_for:
    return parseForExpr();
  default:
    return logError("unknown token when expecting an expression");
  }
}

// binoprhs
//  ::= ('+' primary)*
std::unique_ptr<ExprAST> parseBinOpRHS(int exprPrecedence,
                                       std::unique_ptr<ExprAST> LHS) {
  devPrintf("Parser: parseBinOpRHS (precedence %d)\n", exprPrecedence);
  while (true) {
    int tokPrecedence = getTokPrecedence();

    // If current op has less precedence than prev expr, return prev expr (LHS)
    // -exprPrecedence starts at 0 so returns when no more op
    // -exprPrecedence > 0 in recursive call so returns when LHS has precedence
    // over RHS allowing LHS to be evaluated first
    if (tokPrecedence < exprPrecedence) {
      return LHS;
    }

    int binOp = curTok;
    getNextToken(); // eat binary operator

    auto RHS = parsePrimary();
    if (!RHS) {
      return nullptr;
    }

    // If current op has less precedence than next op, evaluate RHS first
    int nextPrecedence = getTokPrecedence();
    if (tokPrecedence < nextPrecedence) {
      RHS = parseBinOpRHS(tokPrecedence + 1, std::move(RHS));
      if (!RHS) {
        return nullptr;
      }
    }

    // Combine LHS and RHS
    LHS =
        std::make_unique<BinaryExprAST>(binOp, std::move(LHS), std::move(RHS));
  }
}

// expression ::= primary binoprhs
std::unique_ptr<ExprAST> parseExpression() {
  devPrintf("Parser: parseExpression\n");
  auto LHS = parsePrimary();
  if (!LHS) {
    return nullptr;
  }

  return parseBinOpRHS(0, std::move(LHS));
}

// prototype ::= id '(' id* ')'
std::unique_ptr<PrototypeAST> parsePrototype() {
  devPrintf("Parser: parsePrototype\n");
  if (curTok != tok_identifier) {
    return logErrorP("Expected function name in prototype");
  }

  std::string funcName = identifierStr;
  getNextToken();

  if (curTok != '(') {
    return logErrorP("Expected '(' in prototype");
  }

  std::vector<std::string> argNames;
  while (getNextToken() == tok_identifier) {
    argNames.push_back(identifierStr);
  }

  if (curTok != ')') {
    return logErrorP("Expected ')' in prototype");
  }

  getNextToken(); // eat ')'

  return std::make_unique<PrototypeAST>(funcName, std::move(argNames));
}

// definition ::= 'def' prototype expression
std::unique_ptr<FunctionAST> parseDefinition() {
  devPrintf("Parser: parseDefinition\n");
  getNextToken(); // eat def

  auto prototype = parsePrototype();
  if (!prototype) {
    return nullptr;
  }

  if (auto expr = parseExpression()) {
    return std::make_unique<FunctionAST>(std::move(prototype), std::move(expr));
  }

  return nullptr;
}

// toplevelexpr ::= expression
// Allows wrapping bare expression as a function to be handled unifromly later
std::unique_ptr<FunctionAST> parseTopLevelExpr() {
  devPrintf("Parser: parseTopLevelExpr\n");
  if (auto expr = parseExpression()) {
    devPrintf("Parser: create __anon_expr prototype\n");
    auto prototype = std::make_unique<PrototypeAST>("__anon_expr",
                                                    std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(prototype), std::move(expr));
  }

  return nullptr;
}

// external ::= 'extern' prototype
std::unique_ptr<PrototypeAST> parseExtern() {
  devPrintf("Parser: parseExtern\n");
  getNextToken(); // eat extern
  return parsePrototype();
}

} // namespace Compiler
