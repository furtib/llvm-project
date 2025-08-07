//#include <regex>
// RUN: %check_clang_tidy %s bugprone-regex-checker %t

// FIXME: Add something that triggers the check here.
std::regex incorrect("[0-9]++");
// CHECK-MESSAGES: :[[@LINE-1]]:6: warning: function 'f' is insufficiently awesome [bugprone-regex-checker]

// FIXME: Verify the applied fix.
//   * Make the CHECK patterns specific enough and try to make verified lines
//     unique to avoid incorrect matches.
//   * Use {{}} for regular expressions.
// CHECK-FIXES: {{^}}void awesome_f();{{$}}

// FIXME: Add something that doesn't trigger the check here.
std::regex correct("[0-9]+");
