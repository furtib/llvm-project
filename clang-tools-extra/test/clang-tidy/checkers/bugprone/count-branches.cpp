// RUN: %check_clang_tidy -std=c++17 %s bugprone-count-branches %t -- -- -Wno-everything

// Tests for bugprone-count-branches: the check reports, for every condition of
// an if/while/do/for/switch/?:/?:-GNU statement, a summary of the condition:
//
//   var: <distinct variables>  func: <call expressions>
//   and: <&& operators>        or:  <|| operators>
//   deg: <polynomial degree>
//
// Degree encoding (meta info):
//   >= 0 : real polynomial degree
//   -1   : function call (unknown, assumed non-linear)
//   -2   : unknown / unhandled expression
// Sentinel priority under min(): -2 < -1 < 0 <= deg.

double f();       // opaque function -> degree -1
double g(double); // opaque function -> degree -1

void degrees(int x, int y, int z) {
  // --- constants: degree 0 ---
  if (true) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 0 func: 0 and: 0 or: 0 deg: 0 [bugprone-count-branches]
  if (0) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 0 func: 0 and: 0 or: 0 deg: 0 [bugprone-count-branches]
  if ('a') {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 0 func: 0 and: 0 or: 0 deg: 0 [bugprone-count-branches]
  if (3.14) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 0 func: 0 and: 0 or: 0 deg: 0 [bugprone-count-branches]
  if (nullptr) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 0 func: 0 and: 0 or: 0 deg: 0 [bugprone-count-branches]

  // --- single variable: degree 1 ---
  if (x) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  if (!x) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  if (!!!x) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]

  // --- literal * var: degree 1 (regression: was -2 before the fix) ---
  if (2 * x) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  if (x * 2) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  if (3.0 * x) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]

  // --- multiplication ADDS degrees ---
  if (x * x) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 2 [bugprone-count-branches]
  if (x * x * x) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 3 [bugprone-count-branches]
  if ((x * x) * (x * x)) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 4 [bugprone-count-branches]
  if (x * y) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 2 func: 0 and: 0 or: 0 deg: 2 [bugprone-count-branches]

  // --- addition/subtraction take the MAX degree ---
  if (x + y) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 2 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  if (x * x + y) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 2 func: 0 and: 0 or: 0 deg: 2 [bugprone-count-branches]
  if (x * x + y * y) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 2 func: 0 and: 0 or: 0 deg: 2 [bugprone-count-branches]

  // --- division/remainder currently grouped with multiplication ---
  if (x / 2) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  if (x % 2) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]

  // --- sentinels: function (-1) and unknown propagation ---
  if (f()) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 0 func: 1 and: 0 or: 0 deg: -1 [bugprone-count-branches]
  if (x * f()) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 1 and: 0 or: 0 deg: -1 [bugprone-count-branches]
  if (x + f()) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 1 and: 0 or: 0 deg: -1 [bugprone-count-branches]

  // --- THE TRAP: literal * (function | unknown) ---
  // Old logic short-circuited on the literal operand and reported deg 0,
  // silently dropping the non-constant operand. These MUST surface the
  // sentinel instead:
  if (2 * f()) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 0 func: 1 and: 0 or: 0 deg: -1 [bugprone-count-branches]
  if (0 * f()) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 0 func: 1 and: 0 or: 0 deg: -1 [bugprone-count-branches]
  if (2 * ({ int t = x; t; })) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 0 func: 0 and: 0 or: 0 deg: -2 [bugprone-count-branches]
  if (0 + ({ int t = x; t; })) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 0 func: 0 and: 0 or: 0 deg: -2 [bugprone-count-branches]

  // --- THE UNKNOWN SENTINEL (-2) on its own ---
  // A bare GNU statement expression is a StmtExpr: no handler in countDegree
  // recognizes it, so it falls through to the initial `deg = -2`.
  if (({ int t = x; t; })) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 0 func: 0 and: 0 or: 0 deg: -2 [bugprone-count-branches]

  // --- more unknown (-2) cases: other unmodeled expression kinds ---
  if (new int) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 0 func: 0 and: 0 or: 0 deg: -2 [bugprone-count-branches]

  // --- comma operator: value is the RHS only; the LHS is discarded ---
  // The LHS is evaluated for side effects but does not contribute to the
  // condition's value, so it must NOT be counted (vars/funcs/and/or) and must
  // NOT influence the degree. Here the LHS is an unmodeled CXXThrowExpr (-2)
  // and an unmodeled bare LambdaExpr respectively; both are correctly ignored,
  // leaving just the RHS `x` (deg 1).
  if (throw 1, x) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  if (([] { return true; }, x)) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  // A quadratic/call LHS is likewise discarded: only the RHS `x` counts.
  if ((x * x, x)) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  if ((f(), x)) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  // The && in the discarded LHS must not be counted; value is the RHS `y`.
  if ((x && y, y)) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]

  // Ternary whose true-arm is an unmodeled CXXThrowExpr. clang also matches
  // the inner ConditionalOperator, which constant-folds the live arm to `x`
  // (deg 1), so this line emits TWO diagnostics.
  if (x ? throw 1 : x) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: -2 [bugprone-count-branches]
  // CHECK-MESSAGES: :[[@LINE-2]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]

  // --- lambdas: all resolve through a CallExpr, so degree -1 (function) ---
  auto cmp = [](int a) { return a > 0; };
  if ([x] { return x > 0; }()) {} // IIFE -> CallExpr; var 0 (body unwalked)
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 0 func: 1 and: 0 or: 0 deg: -1 [bugprone-count-branches]
  if (cmp(x)) {} // call via lambda var; var 2 (cmp, x)
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 2 func: 1 and: 0 or: 0 deg: -1 [bugprone-count-branches]
  if ([] { return true; }) {} // captureless lambda -> fn-ptr conversion call
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 0 func: 1 and: 0 or: 0 deg: -1 [bugprone-count-branches]

  // --- sizeof / alignof: compile-time constants -> degree 0 ---
  if (sizeof(x)) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 0 func: 0 and: 0 or: 0 deg: 0 [bugprone-count-branches]
  if (sizeof(x) + y) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  if (x * sizeof(x)) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]

  // --- conditional operator: max of the two arms ---
  // Emits two diagnostics: outer if-cond and the inner ConditionalOperator.
  if (x ? x * x : y) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  // CHECK-MESSAGES: :[[@LINE-2]]:7: warning: var: 2 func: 0 and: 0 or: 0 deg: 2 [bugprone-count-branches]

  // --- GNU binary conditional `a ?: b` (needs opaque-value unwrapping) ---
  // Each emits two diagnostics: the if-cond and the BinaryConditionalOperator.
  if (x ?: y) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  // CHECK-MESSAGES: :[[@LINE-2]]:7: warning: var: 2 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  if (x * x ?: y) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 2 [bugprone-count-branches]
  // CHECK-MESSAGES: :[[@LINE-2]]:7: warning: var: 2 func: 0 and: 0 or: 0 deg: 2 [bugprone-count-branches]
  if (x ?: f()) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  // CHECK-MESSAGES: :[[@LINE-2]]:7: warning: var: 1 func: 1 and: 0 or: 0 deg: -1 [bugprone-count-branches]
  if (x ?: (y && z)) {}
  // CHECK-MESSAGES: :[[@LINE-1]]:7: warning: var: 1 func: 0 and: 0 or: 0 deg: 1 [bugprone-count-branches]
  // CHECK-MESSAGES: :[[@LINE-2]]:7: warning: var: 3 func: 0 and: 1 or: 0 deg: 1 [bugprone-count-branches]

  (void)z;
}
