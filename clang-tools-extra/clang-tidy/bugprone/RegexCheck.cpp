//===--- RegexCheck.cpp - clang-tidy --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegexCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <cstdio>
#include <iostream>

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {
void RegexCheck::registerMatchers(MatchFinder *Finder) {
  auto isStdString = qualType(hasUnqualifiedDesugaredType(recordType(
      hasDeclaration(cxxRecordDecl(hasName("::std::basic_string"))))));
  auto containsConcatOp = expr(anyOf(
      cxxOperatorCallExpr(hasOverloadedOperatorName("+")).bind("concat_op"),
      hasDescendant(cxxOperatorCallExpr(hasOverloadedOperatorName("+"))
                        .bind("concat_op"))));
  Finder->addMatcher(
      cxxConstructExpr(
          hasDeclaration(cxxConstructorDecl(ofClass(anyOf(
              hasName("re2::RE2"), classTemplateSpecializationDecl(anyOf(
                                       hasName("std::basic_regex"),
                                       hasName("boost::basic_regex"))))))),
          hasArgument(
              0,
              anyOf(containsConcatOp,
                    ignoringParenImpCasts(
                        declRefExpr(to(varDecl(hasType(isStdString),
                                               hasInitializer(containsConcatOp))
                                           .bind("string_decl")))
                            .bind("string_ref")))))
          .bind("non_stat_constructor"),
      this);
  auto isConstStdString = qualType(
      isConstQualified(), hasUnqualifiedDesugaredType(recordType(hasDeclaration(
                              cxxRecordDecl(hasName("::std::basic_string"))))));
  auto getStringLit = ignoringImplicit(stringLiteral().bind("stringLiteral"));
  auto getStringLiteralFromStdString =
      ignoringImplicit(cxxConstructExpr(hasAnyArgument(getStringLit)));
  auto isConstCharPtr = pointerType(pointee(builtinType(), isConstQualified()));
  Finder->addMatcher(
      cxxConstructExpr(
          hasDeclaration(cxxConstructorDecl(ofClass(anyOf(
              hasName("re2::RE2"), classTemplateSpecializationDecl(anyOf(
                                       hasName("std::basic_regex"),
                                       hasName("boost::basic_regex"))))))),
          hasAnyArgument(ignoringImplicit(anyOf(
              stringLiteral().bind("stringLiteral"),
              declRefExpr(
                  to(varDecl(hasType(isConstStdString),
                             hasInitializer(getStringLiteralFromStdString))))
                  .bind("stringVar"),
              declRefExpr(to(varDecl(hasType(isConstCharPtr),
                                     hasInitializer(getStringLit))))
                  .bind("charptr"),
              memberExpr(member(fieldDecl(hasType(isConstStdString),
                                          hasInClassInitializer(
                                              getStringLiteralFromStdString))))
                  .bind("class_string"),
              memberExpr(member(fieldDecl(hasType(isConstCharPtr),
                                          hasInClassInitializer(getStringLit))))
                  .bind("class_charptr"),
              // new part
              cxxMemberCallExpr(
                  callee(cxxMethodDecl(hasName("begin"))),
                  on(expr(
                      hasType(hasCanonicalType(hasDeclaration(
                          cxxRecordDecl(hasName("::std::basic_string_view"))))),
                      anyOf(declRefExpr(to(varDecl(hasInitializer(hasDescendant(
                                stringLiteral().bind("stringLiteral")))))),
                            memberExpr(member(
                                fieldDecl(hasInClassInitializer(hasDescendant(
                                    stringLiteral().bind("stringLiteral")))))),
                            hasDescendant(
                                stringLiteral().bind("stringLiteral"))))))
                  .bind("string_view")))))
          .bind("x"),
      this);
  Finder->addMatcher(
      cxxConstructExpr(
          hasDeclaration(cxxConstructorDecl(ofClass(anyOf(
              hasName("re2::RE2"), classTemplateSpecializationDecl(anyOf(
                                       hasName("std::basic_regex"),
                                       hasName("boost::basic_regex"))))))))
          .bind("regex_constr"),
      this);
}

std::string sanitize(std::string &s) {
  std::string sanitized("");
  for (char c : s) {
    if (c == '\"' || c == '\\' || c == '$' || c == '`') {
      sanitized.push_back('\\');
    }
    sanitized.push_back(c);
  }
  return sanitized;
}

bool isValidRegex(std::string &&s, int type) {
  std::string cmd("");
  switch(type){
    case 0:
      cmd = "/bin/stdregexvalidator";
      break;
    case 1:
      cmd = "/bin/boostregexvalidator";
      break;
    case 2:
      cmd = "/bin/re2validator";
      break;
      default:
      cmd = "/bin/stdregexvalidator";
  }
  cmd += " \"" + sanitize(s) + "\" > /dev/null";
  std::string result;
  std::array<char, 128> buffer;
  llvm::outs() << cmd << "\n";
  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    std::cerr << "popen() failed!" << std::endl;
    exit(1);
  }
  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    result += buffer.data();
  }
  int returnCode = pclose(pipe);
  return 0 == returnCode;
}

void RegexCheck::check(const MatchFinder::MatchResult &Result) {
  const Expr *expr = Result.Nodes.getNodeAs<Expr>("x");
  if (expr) {
    // diag(expr->getBeginLoc(), "Match Constr!") << expr->getSourceRange();
  }
  const StringLiteral *stringlit =
      Result.Nodes.getNodeAs<StringLiteral>("stringLiteral");
  if (stringlit) {
    diag(stringlit->getBeginLoc(), "String literal in REGEX")
        << stringlit->getSourceRange();
    int type = 0; // 0 std 1 boost 2 re2
    const CXXConstructExpr *constructor =
        Result.Nodes.getNodeAs<CXXConstructExpr>("x");
    if (constructor) {
      const CXXRecordDecl *ClassDecl =
          constructor->getConstructor()->getParent();
      if (ClassDecl) {
        llvm::StringRef ClassName = ClassDecl->getName();
        if(ClassName == "RE2"){
          type = 2;
        } else if (ClassName == "basic_regex"){
          const DeclContext *Context = ClassDecl->getDeclContext();
          if(Context->isStdNamespace())
            type = 0;
          else
            type = 1;
        }
      }
    }
    ///*
    if (!isValidRegex(stringlit->getString().str(), type))
      diag(stringlit->getBeginLoc(), "Invalid!") << stringlit->getSourceRange();
    else
      diag(stringlit->getBeginLoc(), "Valid!") << stringlit->getSourceRange();
    //*/
  }
  const Expr *reg_con = Result.Nodes.getNodeAs<Expr>("non_stat_constructor");
  if (reg_con) {
    diag(reg_con->getBeginLoc(), "non-static") << reg_con->getSourceRange();
  }
  const CXXConstructExpr *reg_constr =
      Result.Nodes.getNodeAs<CXXConstructExpr>("regex_constr");
  if (reg_constr) {
    diag(reg_constr->getBeginLoc(), "Match bare Constr!")
        << reg_constr->getSourceRange();
  }
  // Debug part
  const Expr *unkown = Result.Nodes.getNodeAs<Expr>("whatami");
  if (unkown)
    diag(unkown->getBeginLoc(),
         "unkown! " +
             unkown->getType()->getCanonicalTypeInternal().getAsString())
        << unkown->getSourceRange();
  // End of debug
  return;
}

} // namespace clang::tidy::bugprone
