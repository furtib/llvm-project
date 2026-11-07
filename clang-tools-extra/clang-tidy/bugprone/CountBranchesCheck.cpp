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

// A compile-time constant operand: contributes degree 0 to any surrounding
// arithmetic (e.g. the `2` in `2 * x`). We strip parens/casts once and test
// every constant literal kind clang can produce in an arithmetic context.
static bool isLiteral(const Expr *e) {
  if (!e)
    return false;
  e = e->IgnoreParenCasts();
  if (!e)
    return false;
  return llvm::isa<CXXBoolLiteralExpr,  // true / false
                   CharacterLiteral,    // 'a'
                   StringLiteral,       // "hi"
                   IntegerLiteral,      // 0, 42
                   FloatingLiteral,     // 0.0, 3.14
                   ImaginaryLiteral,    // 2i
                   CXXNullPtrLiteralExpr, // nullptr
                   UnaryExprOrTypeTraitExpr // sizeof(x), alignof(T): compile-time
                                            // constants, argument is unevaluated
                   >(e);
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

// Resolve an OpaqueValueExpr to its underlying source expression, then strip
// parens/casts. clang introduces OpaqueValueExpr to model shared operands
// (notably the common operand of a GNU `a ?: b` BinaryConditionalOperator), so
// any tree walk must unwrap it or it will match none of the node handlers and
// be silently dropped. Loops in case the source expr is itself opaque.
static const Expr *unwrapOpaqueValueExpr(const Expr *e) {
  while (e) {
    e = e->IgnoreParenCasts();
    const auto *o = llvm::dyn_cast_or_null<OpaqueValueExpr>(e);
    if (!o)
      break;
    e = o->getSourceExpr();
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

// Degree encoding (meta info):
//   >= 0 : real polynomial degree
//   -1   : function call (unknown behavior, assumed non-linear)
//   -2   : unknown / unhandled expression
//
// Sentinel priority: -2 (unknown) dominates -1 (function), which dominates any
// real degree. std::min captures this since -2 < -1 < 0 <= deg.

// Combine two sub-expression degrees that are ADDED together (e.g. a + b,
// a - b, the two arms of ?:). The resulting polynomial degree is the max of
// the operands' degrees.
int maxDeg(int lhs, int rhs) {
  if (lhs < 0 || rhs < 0)
    return std::min(lhs, rhs);
  return std::max(lhs, rhs);
}

// Combine two sub-expression degrees that are MULTIPLIED together (e.g. a * b,
// a / b). Polynomial degrees add under multiplication: deg(a*b) = deg a + deg b.
int addDeg(int lhs, int rhs) {
  if (lhs < 0 || rhs < 0)
    return std::min(lhs, rhs);
  return lhs + rhs;
}

// meta info:
// -1 == function
// -2 == Unknown
int CountBranchesCheck::countDegree(const Expr *expr) {
  // Normalize: strip parens/casts and resolve any OpaqueValueExpr (e.g. the
  // shared operand of a GNU `a ?: b`) up front, so every node handler below
  // sees the real underlying expression.
  expr = unwrapOpaqueValueExpr(expr);
  if (!expr)
    return 0;
  if (isLiteral(expr))
    return 0;
  if (isEssentiallyDeclRefExpr(expr))
    return 1;
  // We don't know anything about the function calls, we assume they are non
  // linear
  if (llvm::isa<CallExpr>(expr))
    return -1;
  // If the result is not a unary or binary expr, then we don't know what it is!
  int deg = -2;
  const auto *u = llvm::dyn_cast_or_null<UnaryOperator>(expr);
  if (u)
    deg = countDegree(u->getSubExpr());
  const auto *binaryOp = llvm::dyn_cast_or_null<BinaryOperator>(expr);
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
    // We intentionally ignore this!
    // The comma operator discards its LHS; the value (and thus the degree) of
    // `a, b` is just `b`. Only recurse into the RHS.
    //case BO_Comma:
    //  deg = countDegree(binaryOp->getRHS());
    //  break;
    // overflow into the next one
    case BO_Div:
    case BO_DivAssign:
    case BO_Rem:
    case BO_RemAssign:
    case BO_Mul:
    case BO_MulAssign:
      // Degrees add under multiplication/division: deg(a*b) = deg a + deg b.
      // Literal operands contribute degree 0 (handled by countDegree), so
      // e.g. 2*x -> addDeg(0, 1) = 1 falls out naturally.
      deg = addDeg(countDegree(binaryOp->getLHS()),
                   countDegree(binaryOp->getRHS()));
      break;
    default:
      // Additive and all other binary operators: degree is the max of operands.
      deg = maxDeg(countDegree(binaryOp->getLHS()),
                   countDegree(binaryOp->getRHS()));
      break;
    }
  }

  // If it's a conditional operator, its degree is the max of the two arms.
  const auto *condOp = llvm::dyn_cast_or_null<ConditionalOperator>(expr);
  if (condOp) {
    deg = maxDeg(countDegree(condOp->getTrueExpr()),
                 countDegree(condOp->getFalseExpr()));
  }

  // GNU binary conditional `a ?: b`: the "true" value is the common operand
  // (an OpaqueValueExpr wrapping the condition), the false value is the else.
  // getTrueExpr() here is the OpaqueValueExpr, which countDegree unwraps.
  const auto *binCondOp = llvm::dyn_cast_or_null<BinaryConditionalOperator>(expr);
  if (binCondOp) {
    deg = maxDeg(countDegree(binCondOp->getTrueExpr()),
                 countDegree(binCondOp->getFalseExpr()));
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
  stack.push(expr);
  while (!stack.empty()) {
    const Expr *e = stack.top();
    stack.pop();
    // Strip parens/casts and resolve OpaqueValueExpr on EVERY node (not just
    // the root), so opaque operands introduced inside the tree (e.g. by a GNU
    // `a ?: b`) are not silently dropped.
    e = unwrapOpaqueValueExpr(e);
    if (!e) {
      continue;
    }
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
      // getBase() can be null (e.g. implicit-object member refs); guard it
      // before dereferencing.
      const Expr *rawBase = member->getBase();
      const Expr *base = rawBase ? rawBase->IgnoreParenCasts() : nullptr;
      if (base) {
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
      stack.push(u->getSubExpr());
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
      // The comma operator discards its LHS; the condition value is just the
      // intentionally disabled
      // RHS, so only descend into the RHS (ignore side-effects on the LHS).
      //if (b->getOpcode() == BO_Comma) {
      //  stack.push(b->getRHS());
      //} else {
        stack.push(b->getLHS());
        stack.push(b->getRHS());
      //}
    }
    // walk into parentheses
    const auto *parenExpr =
        llvm::dyn_cast_or_null<ParenExpr>(e->IgnoreParenCasts());
    if (parenExpr) {
      stack.push(parenExpr->getSubExpr());
    }
    const auto *call = llvm::dyn_cast_or_null<CallExpr>(e->IgnoreParenCasts());
    if (call) {
      ans[1] += 1;
      unsigned int argCount = call->getNumArgs();
      const Expr *const *args = call->getArgs();
      for (unsigned int i = 0; i < argCount; i += 1) {
        // A CallExpr arg slot can be null (e.g. elided/default-arg
        // placeholders). Skip nulls; the pop loop normalizes the rest.
        if (args[i])
          stack.push(args[i]);
      }
    }
    // If it's a conditional operator, push its sub-expressions
    const auto *condOp =
        llvm::dyn_cast_or_null<ConditionalOperator>(e->IgnoreParenCasts());
    if (condOp) {
      // stack.push(condOp->getCond()->IgnoreParenCasts()); // The condition
      // will be checked separately
      stack.push(condOp->getTrueExpr());
      stack.push(condOp->getFalseExpr());
    }

    // GNU binary conditional `a ?: b`: traverse both the common operand
    // (getTrueExpr(), an OpaqueValueExpr resolved on pop) and the else value.
    const auto *binCondOp =
        llvm::dyn_cast_or_null<BinaryConditionalOperator>(e);
    if (binCondOp) {
      stack.push(binCondOp->getTrueExpr());
      stack.push(binCondOp->getFalseExpr());
    }
  }
  ans[0] = names.size();
  return ans;
}

void CountBranchesCheck::checkLinearity(const Expr *stmt) {
  if (!stmt)
    return;
  std::array<int, 4> ans = countVarFuncAndOr(stmt);

  diag(stmt->getBeginLoc(), "var: " + llvm::Twine(ans[0]).str() +
                                " func: " + llvm::Twine(ans[1]).str() +
                                " and: " + llvm::Twine(ans[2]).str() +
                                " or: " + llvm::Twine(ans[3]).str() + " deg: " +
                                llvm::Twine(countDegree(stmt)).str())
      << stmt->getSourceRange();
}

void CountBranchesCheck::check(const MatchFinder::MatchResult &Result) {
  checkLinearity(Result.Nodes.getNodeAs<Expr>("cond"));
}

} // namespace bugprone
} // namespace tidy
} // namespace clang
