#include "api/resources/WorkerLimits.h"
#include "api/ApplicationContract.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <stdexcept>
#ifdef __linux__
#include <sys/resource.h>
#endif
#ifdef _OPENMP
#include <omp.h>
#endif
namespace arch::api {
SessionProcessLimits::SessionProcessLimits() {
#ifdef __linux__
    rlimit as{}, cpu{};
    if (getrlimit(RLIMIT_AS, &as) || getrlimit(RLIMIT_CPU, &cpu))
        throw std::runtime_error("Cannot inspect session process limits");
    inherited_cpu_ceiling_ = cpu.rlim_cur;
    as.rlim_cur = std::min<rlim_t>(as.rlim_cur, std::uint64_t(contract::worker_address_space_mib)*1024*1024);
    as.rlim_max = std::min(as.rlim_max, as.rlim_cur);
    if (setrlimit(RLIMIT_AS, &as)) throw std::runtime_error("Cannot limit session address space");
#ifdef _OPENMP
    omp_set_dynamic(0);
    omp_set_num_threads(1);
#endif
    begin_request(contract::case_cpu_seconds);
#else
    throw std::runtime_error("Preview sessions are currently supported on Linux/WSL only");
#endif
}
void SessionProcessLimits::begin_request(int cpu_seconds) {
#ifdef __linux__
    rusage usage{}; rlimit cpu{};
    if (getrusage(RUSAGE_SELF, &usage) || getrlimit(RLIMIT_CPU, &cpu))
        throw std::runtime_error("Cannot inspect session CPU usage");
    const double spent = usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
        (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec)*1.e-6;
    // RLIMIT_CPU is cumulative over the process, not per request. Preserve the
    // inherited lifetime ceiling while setting a fresh soft request deadline.
    cpu.rlim_cur = std::min<rlim_t>({cpu.rlim_max, inherited_cpu_ceiling_,
        static_cast<rlim_t>(std::ceil(spent)) + static_cast<rlim_t>(cpu_seconds)});
    if (setrlimit(RLIMIT_CPU, &cpu)) throw std::runtime_error("Cannot limit session request CPU time");
#else
    (void)cpu_seconds;
#endif
}
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
