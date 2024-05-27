//===--- CountBranchesCheck.cpp - clang-tidy ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CountBranchesCheck.h"
#include "clang/AST/ASTContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace bugprone {

void CountBranchesCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(ifStmt().bind("IfStmt"), this);
  Finder->addMatcher(whileStmt().bind("WhileStmt"), this);
  Finder->addMatcher(doStmt().bind("DoStmt"), this);
  Finder->addMatcher(forStmt().bind("ForStmt"), this);
  Finder->addMatcher(switchStmt().bind("SwitchStmt"), this);
  Finder->addMatcher(conditionalOperator().bind("ConditionalOperator"), this);
  Finder->addMatcher(binaryConditionalOperator().bind("BinaryConditionalOperator"), this);
}

static bool isNumberLiteral(const Expr *e) {
	if (llvm::dyn_cast_or_null<IntegerLiteral>(e->IgnoreParenImpCasts())) return true;
	if (llvm::dyn_cast_or_null<FloatingLiteral>(e->IgnoreParenImpCasts())) return true;
	return false;
}

static bool isEssentiallyDeclRefExpr(const Expr *e) {
	return nullptr != llvm::dyn_cast_or_null<DeclRefExpr>(e->IgnoreParenImpCasts());
}

static bool expressionUsesVariable(const Expr *e) {
	if (!e) return false;
	if (isNumberLiteral(e)) {
		return false;
	}
	if (isEssentiallyDeclRefExpr(e)) {
		return true;
	}
	const auto *binaryOp = llvm::dyn_cast_or_null<BinaryOperator>(e->IgnoreParens());
	if (binaryOp) {
		return expressionUsesVariable(binaryOp->getLHS()) || expressionUsesVariable(binaryOp->getRHS());
	}

	const auto *call = llvm::dyn_cast_or_null<CallExpr>(e);
	if (call) {
		unsigned int argCount = call->getNumArgs();
		const Expr* const *args = call->getArgs();
		for (unsigned int i = 0; i < argCount; i += 1) {
			if (expressionUsesVariable(args[i])) {
				return true;
			}
		}
	}
	return false;
}

static bool callIsLinear(const CallExpr *c);

static bool binaryOpIsLinear(const BinaryOperator *b) {
	if (b == nullptr) return true;
	if (b->isMultiplicativeOp() && expressionUsesVariable(b->getLHS()) && expressionUsesVariable(b->getRHS())) {
		return false;
	}

	const auto *lhsIsBinaryOp = llvm::dyn_cast_or_null<BinaryOperator>(b->getLHS());
	if (lhsIsBinaryOp && !binaryOpIsLinear(lhsIsBinaryOp)) {
		return false;
	}
	const auto *rhsIsBinaryOp = llvm::dyn_cast_or_null<BinaryOperator>(b->getRHS());
	if (rhsIsBinaryOp && !binaryOpIsLinear(rhsIsBinaryOp)) {
		return false;
	}

	const auto *rhsIsCall = llvm::dyn_cast_or_null<CallExpr>(b->getRHS());
	if (rhsIsCall && !callIsLinear(rhsIsCall)) {
		return false;
	}

	const auto *lhsIsCall = llvm::dyn_cast_or_null<CallExpr>(b->getLHS());
	if (lhsIsCall && !callIsLinear(lhsIsCall)) {
		return false;
	}

	return true;
}

static bool isLinearExpr(const Expr *expr);

static bool callIsLinear(const CallExpr *c) {
	const FunctionDecl *f = c->getDirectCallee();
	if (f) f = f->getCanonicalDecl();

	// https://en.cppreference.com/w/cpp/numeric/math
	StringRef nonLinears[] = {
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
	for (unsigned int i = 0; i < sizeof(nonLinears) / sizeof(*nonLinears); i += 1) {
		if (nonLinears[i] == f->getName()) return false;
	}

	unsigned int argCount = c->getNumArgs();
	const Expr* const *args = c->getArgs();
	for (unsigned int i = 0; i < argCount; i += 1) {
		if (!isLinearExpr(args[i])) {
			return false;
		}
	}

	return true;
}

static const Expr* unwrapOpaqueValueExpr(const Expr *e) {
	const OpaqueValueExpr *o = llvm::dyn_cast_or_null<OpaqueValueExpr>(e);
	if (o) { return o->getSourceExpr(); }
	return e;
}

static bool isLinearExpr(const Expr *expr) {
	if (!expr) {
		return false; 
	}

	if (isNumberLiteral(expr)) {
		return true;
	}

	if (isEssentiallyDeclRefExpr(expr)) { 
		return true;
	}

	const auto *b = llvm::dyn_cast_or_null<BinaryOperator>(unwrapOpaqueValueExpr(expr));
	if (b && binaryOpIsLinear(b)) {
		return true;
	}

	const auto *call = llvm::dyn_cast_or_null<CallExpr>(expr);
	if (call && callIsLinear(call)) {
		return true;
	}

	return false;
}

template <typename T>
void CountBranchesCheck::checkLinearity(const T *stmt) {
	if (!stmt) return;
	if (stmt->getCond()) {
		if (isLinearExpr(stmt->getCond())) {
			if (llvm::dyn_cast_or_null<DoStmt>(stmt)) {
				diag(stmt->getEndLoc(), "Linear");
			} else {
				diag(stmt->getBeginLoc(), "Linear");
			}
			Linear += 1;
		} else {
			diag(stmt->getBeginLoc(), "Non-Linear");
		}
	}
}

void CountBranchesCheck::check(const MatchFinder::MatchResult &Result) {
  Total += 1;
  checkLinearity(Result.Nodes.getNodeAs<IfStmt>("IfStmt"));
  checkLinearity(Result.Nodes.getNodeAs<WhileStmt>("WhileStmt"));
  checkLinearity(Result.Nodes.getNodeAs<DoStmt>("DoStmt"));
  checkLinearity(Result.Nodes.getNodeAs<ForStmt>("ForStmt"));
  checkLinearity(Result.Nodes.getNodeAs<SwitchStmt>("SwitchStmt"));
  checkLinearity(Result.Nodes.getNodeAs<ConditionalOperator>("ConditionalOperator"));
  checkLinearity(Result.Nodes.getNodeAs<BinaryConditionalOperator>("BinaryConditionalOperator"));
}

} // namespace bugprone
} // namespace tidy
} // namespace clang
