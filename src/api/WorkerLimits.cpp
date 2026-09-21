#include "WorkerLimits.h"
#include "ApplicationContract.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#ifdef __linux__
#include <sys/resource.h>
#endif
#ifdef _OPENMP
#include <omp.h>
#endif
namespace arch::api {
void ApplyInspectionProcessLimits(int cpu_seconds) {
#ifdef __linux__
    // One request per worker. Tighten inherited limits; never raise them.
    const auto tighten = [](int resource, rlim_t ceiling) {
        rlimit previous{};
        if (getrlimit(resource, &previous) != 0) throw std::runtime_error("Cannot inspect preview process limit");
        previous.rlim_cur = std::min(previous.rlim_cur, ceiling);
        previous.rlim_max = std::min(previous.rlim_max, ceiling);
        if (setrlimit(resource, &previous) != 0) throw std::runtime_error("Cannot install preview process limit");
    };
    tighten(RLIMIT_AS, std::uint64_t(contract::worker_address_space_mib) * 1024 * 1024);
    tighten(RLIMIT_CPU, cpu_seconds);
#ifdef _OPENMP
    omp_set_dynamic(0);
    omp_set_num_threads(1);
#endif
#else
    throw std::runtime_error("Bounded initialization worker is currently supported on Linux only");
#endif
}
}
