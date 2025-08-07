//===--- RegexCheckerCheck.cpp - clang-tidy -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegexCheckerCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

void RegexCheckerCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(cxxConstructExpr().bind("x"), this);
}

bool isValidRegex(std::string&& s){
  llvm::Regex regex(s);
  return regex.isValid();
}

const Expr* InitRoute(const Expr* init){
  // init is never null
  const StringLiteral* str =
    llvm::dyn_cast_or_null<StringLiteral>(init->IgnoreImpCasts());
  if(str){
    if (!isValidRegex(str->getString().str()))
      return str;
  }
  const CXXConstructExpr* string_constr =
    llvm::dyn_cast_or_null<CXXConstructExpr>(init->IgnoreImpCasts());
  if(!string_constr || string_constr->getNumArgs() < 1)
    return nullptr;
  const Expr* arg = string_constr->getArg(0);
  if(!arg)
    return nullptr;
  if(!arg->IgnoreImpCasts())
    return nullptr;
  str = llvm::dyn_cast_or_null<StringLiteral>(arg->IgnoreImpCasts());
  if(str){
    if (!isValidRegex(str->getString().str()))
      return str;
  }
  return nullptr;
}

void RegexCheckerCheck::check(const MatchFinder::MatchResult &Result) {
  const Expr *expr = Result.Nodes.getNodeAs<Expr>("x");
  if(!expr)
    return;
  clang::QualType type = expr->getType();
  if(type->getCanonicalTypeInternal().getAsString().find("std::basic_regex") == std::string::npos)
    return;
  const CXXConstructExpr* constr = llvm::dyn_cast_or_null<CXXConstructExpr>(expr);
  if(!constr)
    return;
  const Expr *arg = constr->getArg(0);
  if(!arg)
    return;
  // StringLiteral as constructor argument
  const StringLiteral* str = llvm::dyn_cast_or_null<StringLiteral>(arg->IgnoreImpCasts());
  if(str){
    if (!isValidRegex(str->getString().str()))
      diag(str->getBeginLoc(), "Invalid regex!") << str->getSourceRange();
  }
  // Variable as constructor arg
  const DeclRefExpr *var = llvm::dyn_cast_or_null<DeclRefExpr>(arg->IgnoreImpCasts());
  if(!var)
    return;
  const ValueDecl* baseDecl = var->getDecl();
  if(!baseDecl)
    return;
  const VarDecl* varDecl = llvm::dyn_cast_or_null<VarDecl>(baseDecl);
  if(!varDecl)
    return;

  // INIT PART
  // getCanonicalDecl solves decls like extern std::string s; to their external definition (is this true tho?)
  const Expr* init = varDecl->getCanonicalDecl()->getInit();
  if(init){
      const Expr* report = InitRoute(init);
      if(report)
        diag(report->getBeginLoc(), "Invalid regex!") << report->getSourceRange();
  }
}

} // namespace clang::tidy::bugprone
