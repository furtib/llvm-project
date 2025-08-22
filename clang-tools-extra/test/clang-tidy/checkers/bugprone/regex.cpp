// RUN: %check_clang_tidy %s bugprone-regex %t
namespace std {
/*
template <typename T>
struct basic_string {
    basic_string();
    void clear();
    bool empty();
    void assign(size_t, const T &);
};

typedef basic_string<char> string;
*/
template <class T>
class basic_regex{
public:
    basic_regex(const T *q){};
    ~basic_regex(){}
};

} // namespace std

namespace boost {

template <class T>
class basic_regex{
public:
    basic_regex(const T *){};
    ~basic_regex(){}
};

} // namespace boosts

// Triggers the check:
void foo(){
    
    std::basic_regex{"[0-9]++"};
    // CHECK-MESSAGES: :[[@LINE-1]]:22: warning: Invalid regex! [bugprone-regex]
    std::basic_regex("");
    // CHECK-MESSAGES: :[[@LINE-1]]:22: warning: Invalid regex! [bugprone-regex]
    std::basic_regex("**");
    // CHECK-MESSAGES: :[[@LINE-1]]:22: warning: Invalid regex! [bugprone-regex]
    std::basic_regex("\\");
    // CHECK-MESSAGES: :[[@LINE-1]]:22: warning: Invalid regex! [bugprone-regex]
    std::basic_regex("AABB???");
    // CHECK-MESSAGES: :[[@LINE-1]]:22: warning: Invalid regex! [bugprone-regex]
    std::basic_regex("AA(C(B)A");
    // CHECK-MESSAGES: :[[@LINE-1]]:22: warning: Invalid regex! [bugprone-regex]
    std::basic_regex("AA(C)B)A");
    // CHECK-MESSAGES: :[[@LINE-1]]:22: warning: Invalid regex! [bugprone-regex]
    std::basic_regex("(w+)(");
    // CHECK-MESSAGES: :[[@LINE-1]]:22: warning: Invalid regex! [bugprone-regex]
    std::basic_regex("[0-9]++");
    // CHECK-MESSAGES: :[[@LINE-1]]:22: warning: Invalid regex! [bugprone-regex]
    boost::basic_regex("");
    // CHECK-MESSAGES: :[[@LINE-1]]:24: warning: Invalid regex! [bugprone-regex]
    boost::basic_regex("**");
    // CHECK-MESSAGES: :[[@LINE-1]]:24: warning: Invalid regex! [bugprone-regex]
    boost::basic_regex("\\");
    // CHECK-MESSAGES: :[[@LINE-1]]:24: warning: Invalid regex! [bugprone-regex]
    boost::basic_regex("AABB???");
    // CHECK-MESSAGES: :[[@LINE-1]]:24: warning: Invalid regex! [bugprone-regex]
    boost::basic_regex("AA(C(B)A");
    // CHECK-MESSAGES: :[[@LINE-1]]:24: warning: Invalid regex! [bugprone-regex]
    boost::basic_regex("AA(C)B)A");
    // CHECK-MESSAGES: :[[@LINE-1]]:24: warning: Invalid regex! [bugprone-regex]
    boost::basic_regex("(w+)(");
    // CHECK-MESSAGES: :[[@LINE-1]]:24: warning: Invalid regex! [bugprone-regex]
    boost::basic_regex("[0-9]++");
    // CHECK-MESSAGES: :[[@LINE-1]]:24: warning: Invalid regex! [bugprone-regex]
}

// FIXME: Add something that doesn't trigger the check here.
//std::basic_regex correct("[0-9]+");
int d;
