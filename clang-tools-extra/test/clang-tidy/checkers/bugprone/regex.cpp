// RUN: %check_clang_tidy %s bugprone-regex %t
namespace std {
template <class T>
class basic_regex {
public:
    basic_regex(const T *q){};
    ~basic_regex(){}
};
typedef basic_regex<char> regex;
} // namespace std


namespace boost {
template <class T>
class basic_regex {
public:
    basic_regex(const T *){};
    ~basic_regex(){}
};
typedef basic_regex<char> regex;
} // namespace boost

namespace re2{
class RE2{
public:
    RE2(const char* q){};
    ~RE2(){};
};
} // namespace re2

// Triggers the check:
void foo(){
    std::regex("(");
    // CHECK-MESSAGES: :[[@LINE-1]]:16: warning: Invalid regex pattern! [bugprone-regex]
    std::regex("+");
    // CHECK-MESSAGES: :[[@LINE-1]]:16: warning: Invalid regex pattern! [bugprone-regex]
    boost::regex("+");
    // CHECK-MESSAGES: :[[@LINE-1]]:18: warning: Invalid regex pattern! [bugprone-regex]
    boost::regex("a**");
    // CHECK-MESSAGES: :[[@LINE-1]]:18: warning: Invalid regex pattern! [bugprone-regex]
    boost::regex("(");
    // CHECK-MESSAGES: :[[@LINE-1]]:18: warning: Invalid regex pattern! [bugprone-regex]
    re2::RE2("+");
    // CHECK-MESSAGES: :[[@LINE-1]]:14: warning: Invalid regex pattern! [bugprone-regex]
    re2::RE2("a**");
    // CHECK-MESSAGES: :[[@LINE-1]]:14: warning: Invalid regex pattern! [bugprone-regex]
    re2::RE2("(");
    // CHECK-MESSAGES: :[[@LINE-1]]:14: warning: Invalid regex pattern! [bugprone-regex]
}

// Valid patterns that do not trigger the check:
void correct_patterns() {
    std::regex correct_std("[0-9]+");
    boost::regex correct_boost("^[a-zA-Z]+$");
    re2::RE2 correct_re2("^[a-zA-Z]+$");

    // Test alternative initializations
    std::regex correct_std2{"[A-Z]*"};
    boost::regex correct_boost2{"(a|b|c)"};
    re2::RE2 correct_re22{"(a|b|c)"};
}
