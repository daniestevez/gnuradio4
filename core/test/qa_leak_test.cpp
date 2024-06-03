#include <fmt/format.h>
#include <boost/ut.hpp>

const boost::ut::suite _leak_test = [] {
    using namespace boost::ut;

    "leak"_test = [] {
        fmt::println("leak test");
        for (int j = 0; j < 1024; ++j) {
            char* p = new char[1024];
        }
    };
};

int
main() { /* not needed for UT */
}
