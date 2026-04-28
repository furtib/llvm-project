// RUN: %check_clang_tidy %s bugprone-regex %t
namespace std {
    template <typename CharT>
    class basic_string {
    public:
        basic_string(const CharT* q) {}
        ~basic_string() {}
    };
    typedef basic_string<char> string;
    template <typename CharT>
    class basic_string_view {
    public:
        basic_string_view(const CharT* q) {}
        const CharT* begin() const { return nullptr; }
        const CharT* end() const { return nullptr; }
    };
    typedef basic_string_view<char> string_view;
    template <class T>
    class basic_regex {
    public:
        basic_regex(const T *q) {}
        basic_regex(const string &s) {}
        template <class ForwardIt>
        basic_regex(ForwardIt first, ForwardIt last) {} 
        ~basic_regex() {}
    };
    typedef basic_regex<char> regex;
} // namespace std


namespace boost {
template <class T>
class basic_regex {
public:
    basic_regex(const T *){};
    basic_regex(const std::string &s) {}
    template <class ForwardIt>
    basic_regex(ForwardIt first, ForwardIt last) {} 
    ~basic_regex(){}
};
typedef basic_regex<char> regex;
} // namespace boost

namespace re2{
class RE2{
public:
    RE2(const char* q){};
    RE2(const std::string &s){};
    template <class ForwardIt>
    RE2(ForwardIt first, ForwardIt last) {} 
    ~RE2(){};
};
} // namespace re2

// Triggers the check:
void foo(){
    std::regex re0("(?<=a)");
    // CHECK-MESSAGES: :[[@LINE-1]]:20: warning: Invalid regex pattern! [bugprone-regex]
    const char* stdrepat1 = "+";
    // CHECK-MESSAGES: :[[@LINE-1]]:29: warning: Invalid regex pattern! [bugprone-regex]
    std::regex re1(stdrepat1);
    const std::string stdrepat2("+");
    // CHECK-MESSAGES: :[[@LINE-1]]:33: warning: Invalid regex pattern! [bugprone-regex]
    std::regex re2(stdrepat2);
    std::string_view stdrepat3("+");
    // CHECK-MESSAGES: :[[@LINE-1]]:32: warning: Invalid regex pattern! [bugprone-regex]
    std::regex re3(stdrepat3.begin(), stdrepat3.end());


    boost::regex re4("+");
    // CHECK-MESSAGES: :[[@LINE-1]]:22: warning: Invalid regex pattern! [bugprone-regex]
    const char* boostrepat1 = "a**";
    // CHECK-MESSAGES: :[[@LINE-1]]:31: warning: Invalid regex pattern! [bugprone-regex]
    boost::regex re5(boostrepat1);
    const std::string boostrepat2("(");
    // CHECK-MESSAGES: :[[@LINE-1]]:35: warning: Invalid regex pattern! [bugprone-regex]
    boost::regex re6(boostrepat2);
    std::string_view boostrepat3("(");
    // CHECK-MESSAGES: :[[@LINE-1]]:34: warning: Invalid regex pattern! [bugprone-regex]
    boost::regex re7(boostrepat3.begin(), boostrepat3.end());


    re2::RE2 re8("(?<=a)");
    // CHECK-MESSAGES: :[[@LINE-1]]:18: warning: Invalid regex pattern! [bugprone-regex]
    const char* re2repat1 = "a**";
    // CHECK-MESSAGES: :[[@LINE-1]]:29: warning: Invalid regex pattern! [bugprone-regex]
    re2::RE2 re9(re2repat1);
    const std::string re2repat2("(");
    // CHECK-MESSAGES: :[[@LINE-1]]:33: warning: Invalid regex pattern! [bugprone-regex]
    re2::RE2 re10(re2repat2);
}

// Valid patterns that do not trigger the check:
void correct_patterns() {
    std::regex correct_std("^[a-zA-Z]+$");
    boost::regex correct_boost("(?<=a)");
    re2::RE2 correct_re2("^[a-zA-Z]+$");

    const char* correct_char = "(a|b|c)";
    std::regex correct_std2(correct_char);
    boost::regex correct_boost2(correct_char);
    re2::RE2 correct_re22(correct_char);

    const std::string correct_str = "[0-9]+";
    std::regex correct_std3(correct_str);
    boost::regex correct_boost3(correct_str);
    re2::RE2 correct_re23(correct_str);

    std::string_view correct_str_view = "a+";
    std::regex correct_std4(correct_str_view.begin(), correct_str_view.end());
    boost::regex correct_boost4(correct_str_view.begin(), correct_str_view.end());

    //Out of scope false negatives:
    // mutable string
    std::string pattern("+");
    std::regex fneg_std(pattern);
    boost::regex fneg_boost(pattern);
    re2::RE2 fneg_re2(pattern);

    // mutable char*
    char* pattern2 = "+";
    std::regex fneg_std2(pattern2);
    boost::regex fneg_boost2(pattern2);
    re2::RE2 fneg_re22(pattern2);
}
