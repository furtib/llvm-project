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
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/TokenKinds.h"
#include "llvm/Support/Casting.h"
#include <llvm/ADT/SmallSet.h>
#include <llvm/ADT/Twine.h>
#include <stack>
#include <algorithm>

using namespace clang::ast_matchers;

namespace clang {
namespace tidy {
namespace bugprone {

void CountBranchesCheck::registerMatchers(MatchFinder *Finder) {
  /*
  Finder->addMatcher(ifStmt().bind("IfStmt"), this);
  Finder->addMatcher(whileStmt().bind("WhileStmt"), this);
  Finder->addMatcher(doStmt().bind("DoStmt"), this);
  Finder->addMatcher(forStmt().bind("ForStmt"), this);
  Finder->addMatcher(switchStmt().bind("SwitchStmt"), this);
  Finder->addMatcher(conditionalOperator().bind("ConditionalOperator"), this); // x ? y : z;
  Finder->addMatcher(binaryConditionalOperator().bind("BinaryConditionalOperator"), this); // x ?: y;
  */
  Finder->addMatcher(mapAnyOf(ifStmt, whileStmt, doStmt, forStmt, switchStmt, conditionalOperator, binaryConditionalOperator)
  .with(hasCondition(expr().bind("cond"))),this);
}

static bool isLiteral(const Expr *e) {
	if (llvm::dyn_cast_or_null<CXXBoolLiteralExpr>(e->IgnoreParenImpCasts())) return true; // what if if(true)?
	if (llvm::dyn_cast_or_null<CharacterLiteral>(e->IgnoreParenImpCasts())) return true; // what if if('a')?
	if (llvm::dyn_cast_or_null<StringLiteral>(e->IgnoreParenImpCasts())) return true; // what if if("hi")?
	if (llvm::dyn_cast_or_null<IntegerLiteral>(e->IgnoreParenImpCasts())) return true; // what if if(0)?
	if (llvm::dyn_cast_or_null<FloatingLiteral>(e->IgnoreParenImpCasts())) return true; // what if if(0.0)?
	return false;
}

static bool isEssentiallyDeclRefExpr(const Expr *e) {
	auto *u = llvm::dyn_cast_or_null<UnaryOperator>(e->IgnoreParenImpCasts()); // what if if(!x)?
	if (u)
		return isEssentiallyDeclRefExpr(u->getSubExpr()); // what if if(!!!!!!!!x)
	return nullptr != llvm::dyn_cast_or_null<DeclRefExpr>(e->IgnoreParenImpCasts());
}

static bool expressionUsesVariable(const Expr *e) {
	if (!e) return false;
	if (isLiteral(e)) {
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

	if (isLiteral(expr)) {
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

int countDegree(const Expr *expr){
	if (!expr) return 0; // this shouldn't occure
	if(isLiteral(expr) || isEssentiallyDeclRefExpr(expr)) return 1;
	int deg = 1;
	const auto *binaryOp = llvm::dyn_cast_or_null<BinaryOperator>(expr->IgnoreParenImpCasts());
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
		switch(binaryOp->getOpcode()){
			// overflow into the next one
			case BO_Div:
			case BO_DivAssign:
			case BO_Rem:
			case BO_RemAssign:
			case BO_Mul:
			case BO_MulAssign:
				if(isLiteral(binaryOp->getLHS()) || isLiteral(binaryOp->getRHS()))
					break;
				deg = 1 + std::max(
					countDegree(binaryOp->getLHS()->IgnoreParenImpCasts()),
					countDegree(binaryOp->getRHS()->IgnoreParenImpCasts())
				);
				break;
			default:
				deg = std::max(
					countDegree(binaryOp->getLHS()->IgnoreParenImpCasts()),
					countDegree(binaryOp->getRHS()->IgnoreParenImpCasts())
				);
				break;
		}
	}
	const auto *callOp = llvm::dyn_cast_or_null<CallExpr>(expr->IgnoreParenImpCasts());
	if(callOp){
		const FunctionDecl *f = callOp->getDirectCallee();
		if(NonLinears.find(f->getCanonicalDecl()->getName()) != NonLinears.end()){ // miért kell a canonical?
			deg++;
		}
	}
	return deg;
}

int countFunctions(const Expr *expr){
	if (!expr) return 0;
	const auto *callExpr = llvm::dyn_cast_or_null<CallExpr>(expr->IgnoreParenImpCasts());
	if (callExpr) {
		return 1;
	}
	int fn = 0;
	const auto *binaryOp = llvm::dyn_cast_or_null<BinaryOperator>(expr->IgnoreParenImpCasts());
	const auto *unaryOp = llvm::dyn_cast_or_null<UnaryOperator>(expr->IgnoreParenImpCasts());
	const auto *conditionalOp = llvm::dyn_cast_or_null<ConditionalOperator>(expr->IgnoreParenImpCasts());
	if (binaryOp || unaryOp || conditionalOp) {
		std::stack<const Expr*> stack;
		if(binaryOp){
			stack.push(binaryOp->getLHS()->IgnoreParenImpCasts());
			stack.push(binaryOp->getRHS()->IgnoreParenImpCasts());
		}
		if(unaryOp){
			stack.push(unaryOp->getSubExpr()->IgnoreParenImpCasts());
		}
		if(conditionalOp){
			stack.push(conditionalOp->getCond()->IgnoreParenImpCasts());
			stack.push(conditionalOp->getTrueExpr()->IgnoreParenImpCasts());
			stack.push(conditionalOp->getFalseExpr()->IgnoreParenImpCasts());
		}
		while (!stack.empty()) {
			const Expr *e = stack.top();
			stack.pop();
			// if its a function call, count it
			const auto *call = llvm::dyn_cast_or_null<CallExpr>(e->IgnoreParenImpCasts());
			if (call) {
				fn++;
				unsigned int argCount = call->getNumArgs();
				const Expr* const *args = call->getArgs();
				for (unsigned int i = 0; i < argCount; i += 1) {
					stack.push(args[i]->IgnoreParenImpCasts());
				}
			}
			// if its an unary operator, push its child
			const auto *u = llvm::dyn_cast_or_null<UnaryOperator>(e->IgnoreParenImpCasts());
			if (u) {
				stack.push(u->getSubExpr()->IgnoreParenImpCasts());
			}
			// if its a binary operator, push its children
			const auto *b = llvm::dyn_cast_or_null<BinaryOperator>(e->IgnoreParenImpCasts());
			if (b) {
				stack.push(b->getLHS()->IgnoreParenImpCasts());
				stack.push(b->getRHS()->IgnoreParenImpCasts());
			}
			// walk into parentheses
			const auto *parenExpr = llvm::dyn_cast_or_null<ParenExpr>(e->IgnoreParenImpCasts());
			if (parenExpr) {
				stack.push(parenExpr->getSubExpr()->IgnoreParenImpCasts());
			}
			const auto *condOp = llvm::dyn_cast_or_null<ConditionalOperator>(e->IgnoreParenImpCasts());
			if (condOp) {
				stack.push(condOp->getCond()->IgnoreParenImpCasts());
				stack.push(condOp->getTrueExpr()->IgnoreParenImpCasts());
				stack.push(condOp->getFalseExpr()->IgnoreParenImpCasts());
			}
		}
	}
	return fn;
}

int countVariables(const Expr *expr){
	if (!expr) return 0;
	if (isEssentiallyDeclRefExpr(expr)) {
		return 1;
	}
	const auto *binaryOp = llvm::dyn_cast_or_null<BinaryOperator>(expr->IgnoreParenImpCasts());
	const auto *conditionalOp = llvm::dyn_cast_or_null<ConditionalOperator>(expr->IgnoreParenImpCasts());
	if (binaryOp || conditionalOp) {
		llvm::SmallSet<std::string, 8> names; // who the hell uses more than 8 vars in one condition
		std::stack<const Expr*> stack;
		if(binaryOp){
			stack.push(binaryOp->getLHS()->IgnoreParenImpCasts());
			stack.push(binaryOp->getRHS()->IgnoreParenImpCasts());
		}
		if(conditionalOp){
			stack.push(conditionalOp->getCond()->IgnoreParenImpCasts());
			stack.push(conditionalOp->getTrueExpr()->IgnoreParenImpCasts());
			stack.push(conditionalOp->getFalseExpr()->IgnoreParenImpCasts());
		}
		while (!stack.empty()) {
			const Expr *e = stack.top();
			stack.pop();
			// if its a variable, count it
			const DeclRefExpr* var = llvm::dyn_cast_or_null<DeclRefExpr>(e->IgnoreParenImpCasts());
			if (var != nullptr && var->getDecl() != nullptr && !names.contains(var->getDecl()->getNameAsString())) {
				names.insert(var->getDecl()->getNameAsString());
			}
			// if its an unary operator, push its child
			const auto *u = llvm::dyn_cast_or_null<UnaryOperator>(e->IgnoreParenImpCasts());
			if (u) {
				stack.push(u->getSubExpr()->IgnoreParenImpCasts());
			}
			// if its a binary operator, push its children
			const auto *b = llvm::dyn_cast_or_null<BinaryOperator>(e->IgnoreParenImpCasts());
			if (b) {
				stack.push(b->getLHS()->IgnoreParenImpCasts());
				stack.push(b->getRHS()->IgnoreParenImpCasts());
			}
			// walk into parentheses
			const auto *parenExpr = llvm::dyn_cast_or_null<ParenExpr>(e->IgnoreParenImpCasts());
			if (parenExpr) {
				stack.push(parenExpr->getSubExpr()->IgnoreImpCasts());
			}
			const auto *call = llvm::dyn_cast_or_null<CallExpr>(e->IgnoreParenImpCasts());
			if (call) {
				unsigned int argCount = call->getNumArgs();
				const Expr* const *args = call->getArgs();
				for (unsigned int i = 0; i < argCount; i += 1) {
					stack.push(args[i]->IgnoreParenImpCasts());
				}
			}
			const auto *condOp = llvm::dyn_cast_or_null<ConditionalOperator>(e->IgnoreParenImpCasts());
			if (condOp) {
				stack.push(condOp->getCond()->IgnoreParenImpCasts());
				stack.push(condOp->getTrueExpr()->IgnoreParenImpCasts());
				stack.push(condOp->getFalseExpr()->IgnoreParenImpCasts());
			}
		}
		return names.size();
	}
	return 0;
}


//template <typename T>
void CountBranchesCheck::checkLinearity(const Expr *stmt) {
	if (!stmt) return;
	//if (stmt->getCond()) {
		if (isLinearExpr(stmt)) {
			diag(stmt->getBeginLoc(), "Linear var: " + llvm::Twine(countVariables(stmt)).str()
				+ " func: " + llvm::Twine(countFunctions(stmt)).str() + " deg: " + llvm::Twine(countDegree(stmt)).str()) << stmt->getSourceRange();
			/*if (llvm::dyn_cast_or_null<DoStmt>(stmt)) {
				diag(stmt->getEndLoc(), "Linear");
			} else {
				diag(stmt->getBeginLoc(), "Linear");
			}*/
			Linear += 1;
		} else {
			diag(stmt->getBeginLoc(), "Non-Linear var: " + llvm::Twine(countVariables(stmt)).str()
				+ " func: " + llvm::Twine(countFunctions(stmt)).str() + " deg: " + llvm::Twine(countDegree(stmt)).str()) << stmt->getSourceRange();
		}
	//}
}

void CountBranchesCheck::check(const MatchFinder::MatchResult &Result) {
  Total += 1;
  checkLinearity(Result.Nodes.getNodeAs<Expr>("cond"));
  /*
  checkLinearity(Result.Nodes.getNodeAs<IfStmt>("IfStmt"));
  checkLinearity(Result.Nodes.getNodeAs<WhileStmt>("WhileStmt"));
  checkLinearity(Result.Nodes.getNodeAs<DoStmt>("DoStmt"));
  checkLinearity(Result.Nodes.getNodeAs<ForStmt>("ForStmt"));
  checkLinearity(Result.Nodes.getNodeAs<SwitchStmt>("SwitchStmt"));
  checkLinearity(Result.Nodes.getNodeAs<ConditionalOperator>("ConditionalOperator"));
  checkLinearity(Result.Nodes.getNodeAs<BinaryConditionalOperator>("BinaryConditionalOperator"));
  */
}

} // namespace bugprone
} // namespace tidy
} // namespace clang
