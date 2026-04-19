.. title:: clang-tidy - bugprone-regex

bugprone-regex
==============

The check detect malformed regex patterns in std::regex, boost::regex and re2::RE2.
It detects patterns defined directly in the constructor call, or defined in a constant variable.

Examples:
std::regex re("(");

const std::string s = "+";
boost::regex re(s);

const char* c = "a++";
re2::RE2 re(c);
