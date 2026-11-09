#include "companion_brain_adapter.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAIL: " << #condition << " at line " << __LINE__ << std::endl; \
    std::exit(1); } } while (false)

int main() {
    const std::string report = bedrockai::companion::run_native_safety_self_check();
    CHECK(report == "READY;engine=ControlHub-v0.35;policy=observe_only;game_state=unavailable;actions=none;finite=yes");
    std::cout << "companion brain adapter tests passed" << std::endl;
    return 0;
}
