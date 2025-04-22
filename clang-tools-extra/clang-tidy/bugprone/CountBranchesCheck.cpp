//===--- CountBranchesCheck.cpp - clang-tidy ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CountBranchesCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <llvm/ADT/SmallSet.h>
#include <llvm/ADT/Twine.h>
#include <stack>

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace bugprone {

void CountBranchesCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(mapAnyOf(ifStmt, whileStmt, doStmt, forStmt, switchStmt,
                              conditionalOperator, binaryConditionalOperator)
                         .with(hasCondition(expr().bind("cond"))),
                     this);
}

static bool isLiteral(const Expr *e) {
  if (llvm::dyn_cast_or_null<CXXBoolLiteralExpr>(e->IgnoreParenCasts()))
    return true; // what if if(true)?
  if (llvm::dyn_cast_or_null<CharacterLiteral>(e->IgnoreParenCasts()))
    return true; // what if if('a')?
  if (llvm::dyn_cast_or_null<StringLiteral>(e->IgnoreParenCasts()))
    return true; // what if if("hi")?
  if (llvm::dyn_cast_or_null<IntegerLiteral>(e->IgnoreParenCasts()))
    return true; // what if if(0)?
  if (llvm::dyn_cast_or_null<FloatingLiteral>(e->IgnoreParenCasts()))
    return true; // what if if(0.0)?
  return false;
}

static bool isEssentiallyDeclRefExpr(const Expr *e) {
  if (!e)
    return false;
  e = e->IgnoreParenCasts();
  if (!e)
    return false;
  auto *u = llvm::dyn_cast_or_null<UnaryOperator>(e); // what if if(!x)?
  if (u) {
    return isEssentiallyDeclRefExpr(u->getSubExpr()); // what if if(!!!!!!!!x)
  }

  if (llvm::isa<DeclRefExpr>(e) || llvm::isa<MemberExpr>(e) ||
      llvm::isa<ArraySubscriptExpr>(e)) {
    return true;
  }
  return false;
}

static const Expr *unwrapOpaqueValueExpr(const Expr *e) {
  const OpaqueValueExpr *o = llvm::dyn_cast_or_null<OpaqueValueExpr>(e);
  if (o) {
    return o->getSourceExpr();
  }
  return e;
}

// store degree of function?
const std::set<StringRef> NonLinears = {
    // Exponential functions
    "exp",
    "expf",
    "expl",
    "exp2",
    "exp2f",
    "exp2l",
    "expm1",
    "expm1f",
    "expm1l",
    "log",
    "logf",
    "logl",
    "log10",
    "log10f",
    "log10l",
    "log2",
    "log2f",
    "log2l",
    "log1p",
    "log1pf",
    "log1pl",

    // Power functions
    "pow",
    "powf",
    "powl",
    "sqrt",
    "sqrtf",
    "sqrtl",
    "cbrt",
    "cbrtf",
    "cbrtl",
    "hypot",
    "hypotf",
    "hypotl",

    // Trigonometric functions
    "sin",
    "sinf",
    "sinl",
    "cos",
    "cosf",
    "cosl",
    "tan",
    "tanf",
    "tanl",
    "asin",
    "asinf",
    "asinl",
    "acos",
    "acosf",
    "acosl",
    "atan",
    "atanf",
    "atanl",
    "atan2",
    "atan2f",
    "atan2l",

    // Hyperbolic functions
    "sinh",
    "sinhf",
    "sinhl",
    "cosh",
    "coshf",
    "coshl",
    "tanh",
    "tanhf",
    "tanhl",
    "asinh",
    "asinhf",
    "asinhl",
    "acosh",
    "acoshf",
    "acoshl",
    "atanh",
    "atanhf",
    "atanhl",
    "atan2",
    "atan2f",
    "atan2l",
};

int CountBranchesCheck::countDegree(const Expr *expr) {
  if (!expr)
    return 0;
  if (isLiteral(expr) || isEssentiallyDeclRefExpr(expr))
    return 1;
  int deg = 1;
  const auto *u =
      llvm::dyn_cast_or_null<UnaryOperator>(expr->IgnoreParenCasts());
  if (u)
    deg = countDegree(u->getSubExpr()->IgnoreParenCasts());
  const auto *binaryOp =
      llvm::dyn_cast_or_null<BinaryOperator>(expr->IgnoreParenCasts());
  if (binaryOp) {
    // multiplicative operators defined by clang:
    // static bool isMultiplicativeOp(Opcode Opc) {
    // 	return Opc >= BO_Mul && Opc <= BO_Rem;
    // }
    // from clang/AST/OperationKinds.def
    // [C99 6.5.5] Multiplicative operators.
    // BINARY_OPERATION(Mul, "*")
    // BINARY_OPERATION(Div, "/")
    // BINARY_OPERATION(Rem, "%")
    switch (binaryOp->getOpcode()) {
    // overflow into the next one
    case BO_Div:
    case BO_DivAssign:
    case BO_Rem:
    case BO_RemAssign:
    case BO_Mul:
    case BO_MulAssign:
      if (isLiteral(binaryOp->getLHS()) || isLiteral(binaryOp->getRHS()))
        break;
      deg = 1 + std::max(countDegree(binaryOp->getLHS()->IgnoreParenCasts()),
                         countDegree(binaryOp->getRHS()->IgnoreParenCasts()));
      break;
    default:
      deg = std::max(countDegree(binaryOp->getLHS()->IgnoreParenCasts()),
                     countDegree(binaryOp->getRHS()->IgnoreParenCasts()));
      break;
    }
  }
  /*
  const auto *callOp =
      llvm::dyn_cast_or_null<CallExpr>(expr->IgnoreParenCasts());
  if (callOp) {
    const FunctionDecl *f = callOp->getDirectCallee();
    if (NonLinears.find(f->getCanonicalDecl()->getName()) !=
        NonLinears.end()) { // miért kell a canonical?
      deg++;                // could also get proper degree from pow, powf, powl
    }

    unsigned int argCount = callOp->getNumArgs();
    const Expr *const *args = callOp->getArgs();
    for (unsigned int i = 0; i < argCount; i += 1) {
      deg = std::max(deg, countDegree(args[i]));
    }
  }
  */
  return deg;
}

// var, func, and, or
std::array<int, 4> CountBranchesCheck::countVarFuncAndOr(const Expr *expr) {
  if (!expr)
    return {0, 0, 0, 0};
  if (isEssentiallyDeclRefExpr(expr)) {
    return {1, 0, 0, 0};
  }
  std::array<int, 4> ans{0, 0, 0, 0};
  llvm::SmallSet<std::string, 8>
      names; // who the hell uses more than 8 vars in one condition
  std::stack<const Expr *> stack;
  stack.push(expr->IgnoreParenCasts());
  while (!stack.empty()) {
    const Expr *e = stack.top();
    stack.pop();
    // if its a variable, count it
    const DeclRefExpr *var =
        llvm::dyn_cast_or_null<DeclRefExpr>(e->IgnoreParenCasts());
    if (var != nullptr && var->getDecl() != nullptr) {
      names.insert(var->getDecl()->getNameAsString());
    }
    const MemberExpr *member =
        llvm::dyn_cast_or_null<MemberExpr>(e->IgnoreParenCasts());
    if (member != nullptr && member->getMemberDecl() != nullptr) {
      std::string name;
      const Expr *base = member->getBase()->IgnoreParenCasts();
      if (base) {
        // arrName +=
        // Lexer::getSourceText(CharSourceRange::getTokenRange(base->getSourceRange()),
        // *SM, LangOptions()).str();
        const DeclRefExpr *baseVar =
            llvm::dyn_cast_or_null<DeclRefExpr>(base->IgnoreParenImpCasts());
        if (baseVar != nullptr && baseVar->getDecl() != nullptr) {
          name += baseVar->getDecl()->getNameAsString() + ".";
        }
        const ArraySubscriptExpr *arr =
            llvm::dyn_cast_or_null<ArraySubscriptExpr>(
                base->IgnoreParenImpCasts());
        if (arr) {
          const DeclRefExpr *idxVar = llvm::dyn_cast_or_null<DeclRefExpr>(
              arr->getBase()->IgnoreParenImpCasts());
          if (idxVar != nullptr && idxVar->getDecl() != nullptr) {
            name += idxVar->getDecl()->getNameAsString();
          }
          // arr->getIdx();
        }
      }
      name += member->getMemberDecl()->getNameAsString();
      names.insert(name); // TODO: concat with arr idx!!!
                          // arrName = name;
    }
    const ArraySubscriptExpr *arr =
        llvm::dyn_cast_or_null<ArraySubscriptExpr>(e->IgnoreParenCasts());
    if (arr != nullptr) {
      const Expr *base = arr->getBase()->IgnoreParenCasts();
      if (base) {
        const DeclRefExpr *baseVar =
            llvm::dyn_cast_or_null<DeclRefExpr>(base->IgnoreParenCasts());
        if (baseVar != nullptr && baseVar->getDecl() != nullptr) {
          names.insert(
              baseVar->getDecl()
                  ->getNameAsString()); // This will be the name of the array
                                        // (maybe concat with idx?)
        }
      }
    }

    // if its an unary operator, push its child
    const auto *u =
        llvm::dyn_cast_or_null<UnaryOperator>(e->IgnoreParenCasts());
    if (u) {
      stack.push(u->getSubExpr()->IgnoreParenCasts());
    }
    // if its a binary operator, push its children
    const auto *b =
        llvm::dyn_cast_or_null<BinaryOperator>(e->IgnoreParenCasts());
    if (b) {
      if (b->getOpcode() == BO_LAnd) {
        ans[2] += 1;
      } else if (b->getOpcode() == BO_LOr) {
        ans[3] += 1;
      }
      stack.push(b->getLHS()->IgnoreParenCasts());
      stack.push(b->getRHS()->IgnoreParenCasts());
    }
    // walk into parentheses
    const auto *parenExpr =
        llvm::dyn_cast_or_null<ParenExpr>(e->IgnoreParenCasts());
    if (parenExpr) {
      stack.push(parenExpr->getSubExpr()->IgnoreParenCasts());
    }
    const auto *call = llvm::dyn_cast_or_null<CallExpr>(e->IgnoreParenCasts());
    if (call) {
      ans[1] += 1;
      unsigned int argCount = call->getNumArgs();
      const Expr *const *args = call->getArgs();
      for (unsigned int i = 0; i < argCount; i += 1) {
        stack.push(args[i]->IgnoreParenCasts());
      }
    }
    // If it's a conditional operator, push its sub-expressions
    const auto *condOp =
        llvm::dyn_cast_or_null<ConditionalOperator>(e->IgnoreParenCasts());
    if (condOp) {
      stack.push(condOp->getCond()->IgnoreParenCasts());
      stack.push(condOp->getTrueExpr()->IgnoreParenCasts());
      stack.push(condOp->getFalseExpr()->IgnoreParenCasts());
    }

    /* These don't work
    // If it's a binary conditional operator, push its sub-expressions
    const auto *binCondOp =
    llvm::dyn_cast_or_null<BinaryConditionalOperator>(e->IgnoreParens()); if
    (binaryOp) { stack.push(binCondOp->getCond());
            stack.push(binCondOp->getTrueExpr());
            stack.push(binCondOp->getFalseExpr());
    }
    */
  }
  ans[0] = names.size();
  return ans;
}

void CountBranchesCheck::checkLinearity(const Expr *stmt) {
  arrName = "";
  if (!stmt)
    return;
  std::array<int, 4> ans = countVarFuncAndOr(stmt);

  diag(stmt->getBeginLoc(), "var: " + llvm::Twine(ans[0]).str() +
                                " func: " + llvm::Twine(ans[1]).str() +
                                " and: " + llvm::Twine(ans[2]).str() +
                                " or: " + llvm::Twine(ans[3]).str() + " deg: " +
                                llvm::Twine(countDegree(stmt)).str() + arrName)
      << stmt->getSourceRange();
}

void CountBranchesCheck::check(const MatchFinder::MatchResult &Result) {
  Total += 1;
  checkLinearity(Result.Nodes.getNodeAs<Expr>("cond"));
  LO = Result.SourceManager;
  SM = &Result.Context->getSourceManager();
}

} // namespace bugprone
} // namespace tidy
} // namespace clang
