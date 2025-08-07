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

void RegexCheckerCheck::check(const MatchFinder::MatchResult &Result) {
  const Expr *expr = Result.Nodes.getNodeAs<Expr>("x");
  if(!expr)
  return;
  clang::QualType type = expr->getType();
  if(type->getCanonicalTypeInternal().getAsString().find("std::basic_regex") != std::string::npos){
    const CXXConstructExpr* constr = llvm::dyn_cast_or_null<CXXConstructExpr>(expr);
    if(!constr)
      return;
    std::string ans = "";
    for(uint i = 0; i < constr->getNumArgs(); ++i){
      const Expr *arg = constr->getArg(i);
      if(!arg)
        continue;
      const StringLiteral* str = llvm::dyn_cast_or_null<StringLiteral>(arg->IgnoreImpCasts());
      if(str){
        llvm::Regex regex(str->getString().str());
        if (!regex.isValid())
          diag(str->getBeginLoc(), "Invalid regex!") << str->getSourceRange();
        ans += std::to_string(i) + str->getString().str() + ";";
      }
    }
  }
}

} // namespace clang::tidy::bugprone
