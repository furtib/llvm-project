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
std::string GetCString(JSContext* ctx, JSValue val) {
    const char* c_str = JS_ToCString(ctx, val);
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
    JSRuntime* runtime;
    JSContext* context;
    JSValue compiled_regex;

    QuickJsRegex(const QuickJsRegex&) = delete;
    QuickJsRegex& operator=(const QuickJsRegex&) = delete;

public:
    QuickJsRegex() : compiled_regex(JS_UNDEFINED) {
        runtime = JS_NewRuntime();
        if (!runtime) {
            //throw std::runtime_error("Failed to create JSRuntime");
        }
        context = JS_NewContext(runtime);
        if (!context) {
            JS_FreeRuntime(runtime);
            //throw std::runtime_error("Failed to create JSContext");
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
    bool compile(const std::string& pattern, std::string& error_out) {
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
        
        compiled_regex = JS_Eval(context, code.c_str(), code.length(), "<input>", JS_EVAL_TYPE_GLOBAL);

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

bool validate_POSIX_BRE(const std::string& regex){
  
}

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
  Finder->addMatcher(cxxConstructExpr(hasDeclaration(
    cxxConstructorDecl(ofClass(classTemplateSpecializationDecl(
                  anyOf(hasName("std::basic_regex"),
                        hasName("boost::basic_regex")
                      )
                    ))
            ))).bind("regex_constr"),this);
}

bool isValidRegex(std::string &&s) {
  QuickJsRegex regex_engine;
  std::string error;
  return regex_engine.compile(s, error);
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
  const Expr* reg_con = Result.Nodes.getNodeAs<Expr>("regex_constr");
  if(reg_con){
    diag(reg_con->getBeginLoc(), "Match bare Constr!") << reg_con->getSourceRange();
    return;
  }
  const Expr *expr = Result.Nodes.getNodeAs<Expr>("x");
  if (expr)
    diag(expr->getBeginLoc(), "Match Constr!") << expr->getSourceRange();
  const StringLiteral *stringlit =
      Result.Nodes.getNodeAs<StringLiteral>("stringLiteral");
  if (stringlit) {
    diag(stringlit->getBeginLoc(), "String literal in REGEX") << stringlit->getSourceRange();
    /*if(!isValidRegex(stringlit->getString().str()))
      diag(stringlit->getBeginLoc(), "Invalid!") << stringlit->getSourceRange();
    else
      diag(stringlit->getBeginLoc(), "Valid!") << stringlit->getSourceRange();*/
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
      diag(init->getBeginLoc(), "INIT!") << init->getSourceRange();
      /*if (isValidRegex(report->getString().str()))
        diag(init->getBeginLoc(), "Valid!") << init->getSourceRange();
      else
        diag(report->getBeginLoc(), "Invalid!") << report->getSourceRange();*/
    }
  }
  return;
}

} // namespace clang::tidy::bugprone
