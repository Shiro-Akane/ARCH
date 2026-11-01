/** Exercise Run-query budgets in disposable child processes. */
#include "api/resources/WorkerLimits.h"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#ifdef __linux__
#include <sys/resource.h>
#endif

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string_view mode = argv[1];
#ifdef __linux__
    rlimit memory_before{}, memory_after{};
    if (getrlimit(RLIMIT_AS, &memory_before)) return 2;
#endif
    if (mode == "wall") {
        arch::api::ExecutionPreflightLimits limits(10, 1);
        std::this_thread::sleep_for(std::chrono::seconds(20));
        return 2;
    }
    if (mode == "cpu") {
        arch::api::ExecutionPreflightLimits limits(1, 15);
        // Steady-clock calls keep this loop observable without scientific work.
        while (std::chrono::steady_clock::now().time_since_epoch().count() != 0) {}
        return 2;
    }
    if (mode != "complete") return 2;
    {
        arch::api::ExecutionPreflightLimits limits(10, 1);
#ifdef __linux__
        if (getrlimit(RLIMIT_AS, &memory_after)
            || memory_after.rlim_cur != memory_before.rlim_cur
            || memory_after.rlim_max != memory_before.rlim_max)
            throw std::runtime_error("Run preflight changed inherited address-space limits");
#endif
    }
    // A completed scope must cancel its deadline instead of killing its caller.
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::cout << "execution preflight deadline cancelled\n";
}
