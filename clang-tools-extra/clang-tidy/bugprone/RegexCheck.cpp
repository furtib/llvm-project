//===--- RegexCheck.cpp - clang-tidy --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegexCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

// Must use extern "C" to include the C headers
extern "C" {
#include "quickjs.h"
}

/**
 * Helper function to extract a C-style string from a JSValue.
 * Remember to free the string with JS_FreeCString!
 */
std::string GetCString(JSContext *ctx, JSValue val) {
  const char *c_str = JS_ToCString(ctx, val);
  if (!c_str) {
    return "[exception]";
  }
  std::string str = c_str;
  JS_FreeCString(ctx, c_str);
  return str;
}

/**
 * A C++ wrapper for a QuickJS regex engine.
 * Manages the lifetime of the JSRuntime and JSContext.
 */
class QuickJsRegex {
private:
  JSRuntime *runtime;
  JSContext *context;
  JSValue compiled_regex;

  QuickJsRegex(const QuickJsRegex &) = delete;
  QuickJsRegex &operator=(const QuickJsRegex &) = delete;

public:
  QuickJsRegex() : compiled_regex(JS_UNDEFINED) {
    runtime = JS_NewRuntime();
    if (!runtime) {
      // throw std::runtime_error("Failed to create JSRuntime");
    }
    context = JS_NewContext(runtime);
    if (!context) {
      JS_FreeRuntime(runtime);
      // throw std::runtime_error("Failed to create JSContext");
    }
  }

  ~QuickJsRegex() {
    JS_FreeValue(context, compiled_regex);

    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
  }

  /**
   * Attempts to compile an ECMA regex pattern.
   * This function does not throw.
   *
   * @param pattern The regex pattern to compile.
   * @param error_out A string to store the error message if compilation fails.
   * @return true if compilation succeeded, false otherwise.
   */
  bool compile(const std::string &pattern, std::string &error_out) {
    // execute the JS code: new RegExp("your_pattern_here")
    // escape the pattern string for use inside a JS string literal.
    // For this simple demo, we'll just escape backslashes.
    // A robust solution would escape quotes, newlines, etc.
    std::string escaped_pattern;
    for (char c : pattern) {
      if (c == '\\') {
        escaped_pattern += "\\\\";
      } else if (c == '"') {
        escaped_pattern += "\\\"";
      } else {
        escaped_pattern += c;
      }
    }

    std::string code = "new RegExp(\"" + escaped_pattern + "\")";

    JS_FreeValue(context, compiled_regex);

    compiled_regex = JS_Eval(context, code.c_str(), code.length(), "<input>",
                             JS_EVAL_TYPE_GLOBAL);

    if (JS_IsException(compiled_regex)) {
      JSValue exception = JS_GetException(context);

      JSValue stack = JS_GetPropertyStr(context, exception, "stack");
      error_out = GetCString(context, stack);

      JS_FreeValue(context, stack);
      JS_FreeValue(context, exception);

      compiled_regex = JS_UNDEFINED;
      return false;
    }

    return true;
  }
};

bool validate_POSIX_BRE(const std::string &regex) {}

void RegexCheck::registerMatchers(MatchFinder *Finder) {
  auto isStdString = qualType(
    hasUnqualifiedDesugaredType(recordType(hasDeclaration(
        cxxRecordDecl(hasName("::std::basic_string"))))));
  auto containsConcatOp = expr(anyOf(
      cxxOperatorCallExpr(hasOverloadedOperatorName("+")).bind("concat_op"),
      hasDescendant(cxxOperatorCallExpr(hasOverloadedOperatorName("+")).bind("concat_op"))
  ));
  Finder->addMatcher(
      cxxConstructExpr(
          hasDeclaration(cxxConstructorDecl(ofClass(anyOf(
              hasName("re2::RE2"), classTemplateSpecializationDecl(anyOf(
                                       hasName("std::basic_regex"),
                                       hasName("boost::basic_regex"))))))),
          hasArgument(
              0,
              anyOf(
                  containsConcatOp,
                  ignoringParenImpCasts(
                      declRefExpr(to(varDecl(hasType(isStdString),
                                             hasInitializer(containsConcatOp))
                                         .bind("string_decl")))
                          .bind("string_ref")))))
          .bind("regex_constro"),
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

bool isValidRegex(std::string &&s) {
  QuickJsRegex regex_engine;
  std::string error;
  return regex_engine.compile(s, error);
}

void RegexCheck::check(const MatchFinder::MatchResult &Result) {
  const Expr *expr = Result.Nodes.getNodeAs<Expr>("x");
  if (expr)
    ; // diag(expr->getBeginLoc(), "Match Constr!") << expr->getSourceRange();
  const StringLiteral *stringlit =
      Result.Nodes.getNodeAs<StringLiteral>("stringLiteral");
  if (stringlit) {
    diag(stringlit->getBeginLoc(), "String literal in REGEX")
        << stringlit->getSourceRange();
    /*if(!isValidRegex(stringlit->getString().str()))
      diag(stringlit->getBeginLoc(), "Invalid!") << stringlit->getSourceRange();
    else
      diag(stringlit->getBeginLoc(), "Valid!") << stringlit->getSourceRange();*/
  }
  const Expr *reg_con = Result.Nodes.getNodeAs<Expr>("regex_constro");
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
