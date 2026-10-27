/**
 * @file WorkerLimits.cpp
 * @brief Enforce process-local preview budgets before expensive setup.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Enforce process-local preview budgets before expensive setup.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#ifdef __linux__
#include <sys/resource.h>
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#ifdef _OPENMP
#include <omp.h>
#endif

#include "api/resources/WorkerLimits.h"

#include "api/ApplicationContract.h"
namespace arch::api {
#ifdef _WIN32
namespace {
constexpr std::uint64_t ticks_per_second = 10'000'000;

HANDLE as_handle(void* handle) {
    return static_cast<HANDLE>(handle);
}

std::runtime_error windows_error(const char* action) {
    return std::runtime_error(std::string(action) + " (Windows error " +
        std::to_string(GetLastError()) + ")");
}

std::uint64_t current_process_user_time() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        throw windows_error("Cannot inspect process user CPU time");
    return (std::uint64_t(user.dwHighDateTime) << 32) | user.dwLowDateTime;
}

std::uint64_t cpu_seconds_to_ticks(int cpu_seconds) {
    if (cpu_seconds <= 0) throw std::invalid_argument("CPU budget must be positive");
    const std::uint64_t seconds = static_cast<std::uint64_t>(cpu_seconds);
    if (seconds > std::numeric_limits<std::uint64_t>::max() / ticks_per_second)
        throw std::overflow_error("Process user CPU budget overflow");
    const std::uint64_t ticks = seconds * ticks_per_second;
    if (ticks > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max()))
        throw std::overflow_error("Process user CPU budget overflow");
    return ticks;
}

std::uint64_t query_job_user_time(HANDLE job) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    DWORD returned = 0;
    if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting,
            sizeof(accounting), &returned))
        throw windows_error("Cannot inspect Windows job user CPU time");
    if (accounting.TotalUserTime.QuadPart < 0)
        throw std::runtime_error("Windows job user CPU time is negative");
    return static_cast<std::uint64_t>(accounting.TotalUserTime.QuadPart);
}

void set_job_end_action(HANDLE job) {
    JOBOBJECT_END_OF_JOB_TIME_INFORMATION action{};
    action.EndOfJobTimeAction = JOB_OBJECT_TERMINATE_AT_END_OF_JOB;
    if (!SetInformationJobObject(job, JobObjectEndOfJobTimeInformation, &action,
            sizeof(action)))
        throw windows_error("Cannot require termination at the Windows job CPU limit");
}

void configure_new_resource_job(HANDLE job, std::uint64_t memory_bytes,
        std::uint64_t job_user_budget) {
    if (job == nullptr || job == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Windows resource job is not available");
    if (memory_bytes > static_cast<std::uint64_t>(std::numeric_limits<SIZE_T>::max()))
        throw std::overflow_error("Windows commit limit does not fit SIZE_T");
    if (job_user_budget == 0 ||
        job_user_budget > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max()))
        throw std::overflow_error("Windows job user CPU budget overflow");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_JOB_TIME;
    limits.ProcessMemoryLimit = static_cast<SIZE_T>(memory_bytes);
    limits.BasicLimitInformation.PerJobUserTimeLimit.QuadPart =
        static_cast<LONGLONG>(job_user_budget);
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        throw windows_error("Cannot install Windows memory and job CPU limits");
    set_job_end_action(job);
}

void set_job_cpu_budget(HANDLE job, std::uint64_t job_user_budget) {
    if (job == nullptr || job == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Windows resource job is not available");
    if (job_user_budget == 0 ||
        job_user_budget > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max()))
        throw std::overflow_error("Windows job user CPU budget overflow");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    DWORD returned = 0;
    if (!QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
            sizeof(limits), &returned))
        throw windows_error("Cannot inspect Windows resource limits");
    if (!(limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_PROCESS_MEMORY) ||
        limits.ProcessMemoryLimit == 0)
        throw std::runtime_error("Windows commit limit is not enforced");
    limits.BasicLimitInformation.LimitFlags &=
        ~static_cast<DWORD>(JOB_OBJECT_LIMIT_PROCESS_TIME | JOB_OBJECT_LIMIT_PRESERVE_JOB_TIME);
    limits.BasicLimitInformation.LimitFlags |=
        JOB_OBJECT_LIMIT_JOB_TIME;
    limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart = 0;
    // The kernel adds user CPU already spent in the job. Supply only the new
    // request allowance; adding the cumulative time here would grant it twice.
    // JOB_TIME and PRESERVE_JOB_TIME are mutually exclusive. This job never
    // changes the limits of its parents in the native process supervisor.
    limits.BasicLimitInformation.PerJobUserTimeLimit.QuadPart =
        static_cast<LONGLONG>(job_user_budget);
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        throw windows_error("Cannot renew Windows job user CPU budget");
    set_job_end_action(job);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION installed{};
    if (!QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &installed,
            sizeof(installed), &returned))
        throw windows_error("Cannot verify Windows resource limits");
    if (!(installed.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_JOB_TIME) ||
        !(installed.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_PROCESS_MEMORY))
        throw std::runtime_error("Windows resource limits were not applied");
}

HANDLE create_resource_job(std::uint64_t memory_bytes, std::uint64_t job_user_budget) {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) throw windows_error("Cannot create Windows resource job");
    try {
        configure_new_resource_job(job, memory_bytes, job_user_budget);
        if (!AssignProcessToJobObject(job, GetCurrentProcess()))
            throw windows_error("Cannot join nested Windows resource job");
        BOOL in_job = FALSE;
        if (!IsProcessInJob(GetCurrentProcess(), job, &in_job) || !in_job)
            throw windows_error("Cannot verify nested Windows resource job membership");
    } catch (...) {
        CloseHandle(job);
        throw;
    }
    return job;
}

WindowsResourceSnapshot query_windows_resource_limits(HANDLE job) {
    if (job == nullptr || job == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Windows resource job is not available");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    DWORD returned = 0;
    if (!QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
            sizeof(limits), &returned))
        throw windows_error("Cannot query Windows resource limits");
    JOBOBJECT_END_OF_JOB_TIME_INFORMATION end_action{};
    if (!QueryInformationJobObject(job, JobObjectEndOfJobTimeInformation, &end_action,
            sizeof(end_action), &returned))
        throw windows_error("Cannot query Windows end-of-job action");

    WindowsResourceSnapshot snapshot;
    snapshot.commit_limit_bytes = limits.ProcessMemoryLimit;
    snapshot.job_user_limit_100ns =
        static_cast<std::uint64_t>(limits.BasicLimitInformation.PerJobUserTimeLimit.QuadPart);
    snapshot.job_user_time_100ns = query_job_user_time(job);
    snapshot.limit_flags = limits.BasicLimitInformation.LimitFlags;
    snapshot.end_of_job_action = end_action.EndOfJobTimeAction;
    snapshot.commit_limit_enforced =
        (limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_PROCESS_MEMORY) != 0 &&
        limits.ProcessMemoryLimit != 0;
    snapshot.job_user_limit_enforced =
        (limits.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_JOB_TIME) != 0;
    snapshot.job_terminate_at_limit =
        end_action.EndOfJobTimeAction == JOB_OBJECT_TERMINATE_AT_END_OF_JOB;
    return snapshot;
}

void install_single_thread_openmp() {
#ifdef _OPENMP
    omp_set_dynamic(0);
    omp_set_num_threads(1);
#endif
}

struct InspectionResourceJob {
    HANDLE handle = nullptr;
    std::uint64_t cumulative_ceiling_100ns = 0;

    explicit InspectionResourceJob(int cpu_seconds) {
        const std::uint64_t ceiling = cpu_seconds_to_ticks(cpu_seconds);
        const std::uint64_t spent = current_process_user_time();
        if (spent >= ceiling)
            throw std::runtime_error("Inspection process CPU budget is already exhausted");
        handle = create_resource_job(
            static_cast<std::uint64_t>(contract::windows_worker_commit_mib) * 1024 * 1024,
            ceiling - spent);
        cumulative_ceiling_100ns = ceiling;
    }
    void apply(int cpu_seconds) {
        const std::uint64_t requested = cpu_seconds_to_ticks(cpu_seconds);
        const std::uint64_t ceiling = std::min(cumulative_ceiling_100ns, requested);
        const std::uint64_t spent = current_process_user_time();
        if (spent >= ceiling)
            throw std::runtime_error("Inspection process CPU budget is already exhausted");
        set_job_cpu_budget(handle, ceiling - spent);
        cumulative_ceiling_100ns = ceiling;
    }
    ~InspectionResourceJob() {
        if (handle != nullptr) CloseHandle(handle);
    }
    InspectionResourceJob(const InspectionResourceJob&) = delete;
    InspectionResourceJob& operator=(const InspectionResourceJob&) = delete;
};
}
#endif

/** Remember process limits for repeated local preview requests. */
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
#elif defined(_WIN32)
    install_single_thread_openmp();
    HANDLE job = create_resource_job(
        static_cast<std::uint64_t>(contract::windows_worker_commit_mib) * 1024 * 1024,
        cpu_seconds_to_ticks(contract::case_cpu_seconds));
    resource_job_ = job;
    try {
        begin_request(contract::case_cpu_seconds);
    } catch (...) {
        CloseHandle(job);
        resource_job_ = nullptr;
        throw;
    }
#else
    throw std::runtime_error("Preview sessions are currently supported on Linux/WSL only");
#endif
}

SessionProcessLimits::~SessionProcessLimits() {
#ifdef _WIN32
    if (resource_job_ != nullptr) CloseHandle(as_handle(resource_job_));
#endif
}

/** Apply a fresh CPU budget before serving one session request. */
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
#elif defined(_WIN32)
    set_job_cpu_budget(as_handle(resource_job_), cpu_seconds_to_ticks(cpu_seconds));
#else
    (void)cpu_seconds;
#endif
}

#ifdef _WIN32
WindowsResourceSnapshot SessionProcessLimits::inspect_windows_resource_limits() const {
    return query_windows_resource_limits(as_handle(resource_job_));
}
#endif

/** Constrain a one-shot inspection worker before expensive setup. */
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
#elif defined(_WIN32)
    install_single_thread_openmp();
    static InspectionResourceJob inspection(cpu_seconds);
    inspection.apply(cpu_seconds);
#else
    throw std::runtime_error("Bounded initialization worker is currently supported on Linux only");
#endif
}
}
