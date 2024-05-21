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
  Finder->addMatcher(ifStmt().bind("ifStmt"), this);
  Finder->addMatcher(whileStmt().bind("whileStmt"), this);
  Finder->addMatcher(doStmt().bind("doStmt"), this);
  Finder->addMatcher(forStmt().bind("forStmt"), this);
  Finder->addMatcher(switchStmt().bind("switchStmt"), this);
}

void CountBranchesCheck::check(const MatchFinder::MatchResult &Result) {

  const auto *ifStmt = Result.Nodes.getNodeAs<IfStmt>("ifStmt");
  if (ifStmt != NULL) diag(ifStmt->getBeginLoc(), "Found: if");

  const auto *whileStmt = Result.Nodes.getNodeAs<WhileStmt>("whileStmt");
  if (whileStmt != NULL) diag(whileStmt->getBeginLoc(), "Found: while");

  const auto *doStmt = Result.Nodes.getNodeAs<DoStmt>("doStmt");
  if (doStmt != NULL) diag(doStmt->getBeginLoc(), "Found: do-while");

  const auto *forStmt = Result.Nodes.getNodeAs<ForStmt>("forStmt");
  if (forStmt != NULL) diag(forStmt->getBeginLoc(), "Found: for");

  const auto *switchStmt = Result.Nodes.getNodeAs<SwitchStmt>("switchStmt");
  if (switchStmt != NULL) diag(switchStmt->getBeginLoc(), "Found: switch");
}

} // namespace bugprone
} // namespace tidy
} // namespace clang
