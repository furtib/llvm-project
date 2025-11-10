//===--- RegexCheck.cpp - clang-tidy --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegexCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <re2/re2.h>

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

void RegexCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      cxxConstructExpr(
          hasDeclaration(
              cxxConstructorDecl(ofClass(classTemplateSpecializationDecl(
                  anyOf(hasName("std::basic_regex"),
                        hasName("boost::basic_regex")))))),
          hasAnyArgument(ignoringImplicit(anyOf(
              stringLiteral().bind("stringLiteral"),
              declRefExpr(to(varDecl(
                  hasType(qualType(
                      isConstQualified(),
                      hasUnqualifiedDesugaredType(recordType(hasDeclaration(
                          cxxRecordDecl(hasName("::std::basic_string"))))))))))
                  .bind("stringVar"),
              declRefExpr(hasType(pointerType(
                              pointee(builtinType(), isConstQualified()))))
                  .bind("charptr")))))
          .bind("x"),
      this);
}

bool isValidRegex(std::string &&s) {
  RE2::Options options;
  options.set_log_errors(false);
  RE2 regex(s, options);
  return regex.ok();
}

// This function tries to retrive the string literal from str and const char* variables 
const StringLiteral *getStrFromInitialization(const Expr *init) {
  // init is never null
  const StringLiteral *str =
      llvm::dyn_cast_or_null<StringLiteral>(init->IgnoreImpCasts());
  if (str)
      return str;
  const CXXConstructExpr *string_constr =
      llvm::dyn_cast_or_null<CXXConstructExpr>(init->IgnoreImpCasts());
  if (!string_constr || string_constr->getNumArgs() < 1)
    return nullptr;
  const Expr *arg = string_constr->getArg(0);
  if (!arg)
    return nullptr;
  if (!arg->IgnoreImpCasts())
    return nullptr;
  str = llvm::dyn_cast_or_null<StringLiteral>(arg->IgnoreImpCasts());
  if (str)
      return str;
  return nullptr;
}

void RegexCheck::check(const MatchFinder::MatchResult &Result) {
  const Expr *expr = Result.Nodes.getNodeAs<Expr>("x");
  if (expr)
    diag(expr->getBeginLoc(), "Match Constr!") << expr->getSourceRange();
  const StringLiteral *stringlit =
      Result.Nodes.getNodeAs<StringLiteral>("stringLiteral");
  if (stringlit) {
    if(!isValidRegex(stringlit->getString().str()))
      diag(stringlit->getBeginLoc(), "Invalid!") << stringlit->getSourceRange();
    else
      diag(stringlit->getBeginLoc(), "Valid!") << stringlit->getSourceRange();
    return;
  }
  const DeclRefExpr *stringvar =
      Result.Nodes.getNodeAs<DeclRefExpr>("stringVar");
  if (stringvar)
    diag(stringvar->getBeginLoc(), "StringVar!") << stringvar->getSourceRange();
  const DeclRefExpr *charptr = Result.Nodes.getNodeAs<DeclRefExpr>("charptr");
  if (charptr)
    diag(charptr->getBeginLoc(), "Charptr!") << charptr->getSourceRange();
  // Debug part
  const Expr *unkown = Result.Nodes.getNodeAs<Expr>("whatami");
  if (unkown)
    diag(unkown->getBeginLoc(), "unkown! " + unkown->getType()->getCanonicalTypeInternal().getAsString()) << unkown->getSourceRange();
  // End of debug
  const ValueDecl *baseDecl = nullptr;
  if (stringvar)
    baseDecl = stringvar->getDecl();
  else if (charptr)
    baseDecl = charptr->getDecl();
  if (!baseDecl)
    return;
    
  const VarDecl *varDecl = llvm::dyn_cast_or_null<VarDecl>(baseDecl);
  if (!varDecl)
    return;

  // INIT PART
  // getCanonicalDecl solves decls like extern std::string s;
  // to their external definition (is this true tho?)
  const Expr *init = varDecl->getCanonicalDecl()->getInit();
  if (init) {
    const StringLiteral *report = getStrFromInitialization(init);
    if(report){
      if (isValidRegex(report->getString().str()))
        diag(init->getBeginLoc(), "Valid!") << init->getSourceRange();
      else
        diag(report->getBeginLoc(), "Invalid!") << report->getSourceRange();
    }
  }
  return;
}

} // namespace clang::tidy::bugprone
