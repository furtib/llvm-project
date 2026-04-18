//===--- RegexCheck.cpp - clang-tidy --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegexCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <boost/regex.hpp>
#include <re2/re2.h>
#include <regex>

namespace boost {
void throw_exception(std::exception const &e) {
  // Route the Boost error into LLVM's crash handler
  llvm::report_fatal_error(llvm::StringRef("Boost regex error: ") + e.what());
}
} // namespace boost

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {
void RegexCheck::registerMatchers(MatchFinder *Finder) {
  // non-static matcher
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
  // main matcher
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
  // constructor matcher
  Finder->addMatcher(
      cxxConstructExpr(
          hasDeclaration(cxxConstructorDecl(ofClass(anyOf(
              hasName("re2::RE2"), classTemplateSpecializationDecl(anyOf(
                                       hasName("std::basic_regex"),
                                       hasName("boost::basic_regex"))))))))
          .bind("regex_constr"),
      this);
}

std::pair<bool, std::string> isValidRegex(std::string &&s, int type) {
  switch (type) {
  case 0:
    try {
      std::regex re(s);
      return std::pair(true,"");
    } catch (std::regex_error &e) {
      return std::pair(false, e.what());
    }
    break;
  case 1: // boost regex
  {
    try {
      boost::regex re(s);
      return {true, ""};
    } catch (const boost::regex_error& e) {
        return {false, e.what()};
    }
  } break;
  case 2: {
    RE2 re(s);
    return std::pair(re.ok(), re.error());
  } break;
  default:
    try {
      std::regex re(s);
      return std::pair(true,"");
    } catch (std::regex_error &e) {
      return std::pair(false, e.what());
    }
  }
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

    // figure out which constructor was matched.
    int type = 0; // 0 std 1 boost 2 re2
    const CXXConstructExpr *constructor =
        Result.Nodes.getNodeAs<CXXConstructExpr>("x");
    if (constructor) {
      const CXXRecordDecl *ClassDecl =
          constructor->getConstructor()->getParent();
      if (ClassDecl) {
        llvm::StringRef ClassName = ClassDecl->getName();
        if (ClassName == "RE2") {
          type = 2;
        } else if (ClassName == "basic_regex") {
          const DeclContext *Context = ClassDecl->getDeclContext();
          if (Context->isStdNamespace())
            type = 0;
          else
            type = 1;
        }
      }
    }

    // Check validity of the pattern
    std::pair<bool, std::string> validity = isValidRegex(stringlit->getString().str(), type);
    if (!validity.first)
      diag(stringlit->getBeginLoc(), "Invalid regex pattern!") << stringlit->getSourceRange();
      //diag(stringlit->getBeginLoc(), "Invalid regex pattern! " + validity.second) << stringlit->getSourceRange();
    else
      diag(stringlit->getBeginLoc(), "Valid!") << stringlit->getSourceRange();
      //diag(stringlit->getBeginLoc(), "Valid!") << stringlit->getSourceRange();
  }

  // Search for definitely non-static patterns
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
