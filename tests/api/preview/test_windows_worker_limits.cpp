#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "api/ApplicationContract.h"
#include "api/resources/WorkerLimits.h"

namespace {
std::string json_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (ch < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                escaped += "\\u00";
                escaped += hex[ch >> 4];
                escaped += hex[ch & 0x0f];
            } else {
                escaped += static_cast<char>(ch);
            }
        }
    }
    return escaped;
}
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#include <algorithm>
#include <limits>
#include <vector>

namespace {
constexpr std::uint64_t mib = 1024ULL * 1024ULL;
constexpr std::uint64_t ticks_per_ms = 10'000ULL;
constexpr DWORD cpu_fallback_exit = 0xBEEF;

void print_error(const std::string& test, const std::string& message) {
    std::cout << "{\"test\":\"" << json_escape(test) << "\",\"status\":\"FAIL\","
              << "\"error\":\"" << json_escape(message) << "\"}\n";
}

std::runtime_error windows_error(const char* action) {
    return std::runtime_error(std::string(action) + " (Windows error " +
        std::to_string(GetLastError()) + ")");
}

std::uint64_t combine_filetime(const FILETIME& value) {
    return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}

std::uint64_t process_user_ticks() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        throw windows_error("Cannot query process user CPU time");
    return combine_filetime(user);
}

std::uint64_t process_user_ms() {
    return process_user_ticks() / ticks_per_ms;
}

bool burn_user_cpu(std::uint64_t target_ms, std::uint64_t wall_limit_ms) {
    const std::uint64_t start_user = process_user_ticks();
    const ULONGLONG start_wall = GetTickCount64();
    volatile double accumulator = 0.0;
    while (process_user_ticks() - start_user < target_ms * ticks_per_ms) {
        for (int index = 0; index < 2048; ++index)
            accumulator = accumulator * 1.0000001 + static_cast<double>(index);
        if (GetTickCount64() - start_wall > wall_limit_ms) return false;
    }
    return true;
}

struct JobView {
    std::uint64_t commit_limit_bytes = 0;
    std::uint64_t process_user_limit_100ns = 0;
    std::uint64_t job_user_limit_100ns = 0;
    std::uint64_t total_user_time_100ns = 0;
    std::uint32_t flags = 0;
    std::uint32_t end_of_job_action = 0;
    DWORD active_processes = 0;
};

JobView query_job(HANDLE job) {
    if (job == nullptr || job == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Test job handle is not available");
    JobView view;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    DWORD returned = 0;
    if (!QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
            sizeof(limits), &returned))
        throw windows_error("Cannot query test job limits");
    view.commit_limit_bytes = limits.ProcessMemoryLimit;
    view.process_user_limit_100ns =
        static_cast<std::uint64_t>(limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart);
    view.job_user_limit_100ns =
        static_cast<std::uint64_t>(limits.BasicLimitInformation.PerJobUserTimeLimit.QuadPart);
    view.flags = limits.BasicLimitInformation.LimitFlags;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting,
            sizeof(accounting), &returned))
        throw windows_error("Cannot query test job accounting");
    view.total_user_time_100ns =
        static_cast<std::uint64_t>(accounting.TotalUserTime.QuadPart);
    view.active_processes = accounting.ActiveProcesses;
    JOBOBJECT_END_OF_JOB_TIME_INFORMATION end_action{};
    if (!QueryInformationJobObject(job, JobObjectEndOfJobTimeInformation, &end_action,
            sizeof(end_action), &returned))
        throw windows_error("Cannot query test end-of-job action");
    view.end_of_job_action = end_action.EndOfJobTimeAction;
    return view;
}

HANDLE create_parent_job(std::uint64_t memory_bytes, std::uint64_t cpu_seconds) {
    if (memory_bytes > std::numeric_limits<SIZE_T>::max())
        throw std::overflow_error("Parent commit limit does not fit SIZE_T");
    const std::uint64_t deadline = cpu_seconds * 10'000'000ULL;
    if (deadline > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max()))
        throw std::overflow_error("Parent CPU deadline overflow");
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) throw windows_error("Cannot create parent test job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_PROCESS_MEMORY | JOB_OBJECT_LIMIT_PROCESS_TIME;
    limits.ProcessMemoryLimit = static_cast<SIZE_T>(memory_bytes);
    limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart =
        static_cast<LONGLONG>(deadline);
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        CloseHandle(job);
        throw windows_error("Cannot set parent test job limits");
    }
    return job;
}

std::wstring executable_path() {
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
        throw windows_error("Cannot resolve test executable path");
    return std::wstring(path.data(), length);
}

struct ChildResult {
    DWORD exit_code = 0;
    bool timed_out = false;
    std::uint64_t elapsed_ms = 0;
    std::string output;
    JobView job;
};

ChildResult run_contained_child(const std::string& child_mode, std::uint64_t parent_memory,
        std::uint64_t parent_cpu_seconds, DWORD timeout_ms) {
    HANDLE job = create_parent_job(parent_memory, parent_cpu_seconds);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
        CloseHandle(job);
        throw windows_error("Cannot create test output pipe");
    }
    SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

    std::wstring command = L"\"" + executable_path() + L"\" --child " +
        std::wstring(child_mode.begin(), child_mode.end());
    std::vector<wchar_t> command_line(command.begin(), command.end());
    command_line.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    PROCESS_INFORMATION process{};
    const ULONGLONG started = GetTickCount64();
    if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        const DWORD error = GetLastError();
        CloseHandle(write_pipe);
        CloseHandle(read_pipe);
        CloseHandle(job);
        throw std::runtime_error("Cannot create contained test child (Windows error " +
            std::to_string(error) + ")");
    }
    CloseHandle(write_pipe);
    write_pipe = nullptr;

    try {
        if (!AssignProcessToJobObject(job, process.hProcess))
            throw windows_error("Cannot assign test child to parent job");
        if (ResumeThread(process.hThread) == static_cast<DWORD>(-1))
            throw windows_error("Cannot resume test child");
    } catch (...) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(read_pipe);
        CloseHandle(job);
        throw;
    }

    ChildResult result;
    const DWORD wait = WaitForSingleObject(process.hProcess, timeout_ms);
    result.elapsed_ms = GetTickCount64() - started;
    result.timed_out = wait == WAIT_TIMEOUT;
    if (result.timed_out) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
    }
    if (!GetExitCodeProcess(process.hProcess, &result.exit_code)) {
        result.exit_code = ERROR_PROCESS_ABORTED;
    }
    result.job = query_job(job);

    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(read_pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        result.output.append(buffer, read);

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(read_pipe);
    CloseHandle(job);
    while (!result.output.empty() &&
            (result.output.back() == '\n' || result.output.back() == '\r'))
        result.output.pop_back();
    return result;
}

bool child_passed(const ChildResult& child) {
    return !child.timed_out && child.exit_code == 0 &&
        child.output.find("\"status\":\"PASS\"") != std::string::npos;
}

std::string child_json(const ChildResult& child) {
    if (child.output.size() >= 2 && child.output.front() == '{' && child.output.back() == '}')
        return child.output;
    return "{\"status\":\"FAIL\",\"error\":\"" + json_escape(child.output) + "\"}";
}

void print_child_job(const ChildResult& child) {
    std::cout << ",\"exit_code\":" << child.exit_code
              << ",\"timed_out\":" << (child.timed_out ? "true" : "false")
              << ",\"elapsed_ms\":" << child.elapsed_ms
              << ",\"parent_commit_limit_bytes\":" << child.job.commit_limit_bytes
              << ",\"parent_process_user_limit_100ns\":"
              << child.job.process_user_limit_100ns
              << ",\"parent_job_user_limit_100ns\":"
              << child.job.job_user_limit_100ns
              << ",\"parent_flags\":" << child.job.flags
              << ",\"parent_end_of_job_action\":" << child.job.end_of_job_action
              << ",\"parent_total_user_time_100ns\":" << child.job.total_user_time_100ns;
}

int run_info(bool startup = false) {
    if (startup && !burn_user_cpu(600, 3000))
        throw std::runtime_error("Startup CPU probe exceeded its wall guard");
    arch::api::SessionProcessLimits session;
    const auto snapshot = session.inspect_windows_resource_limits();
    const std::uint64_t current_user = process_user_ticks();
    int openmp_threads = 0;
    int openmp_dynamic = -1;
#ifdef _OPENMP
    openmp_threads = omp_get_max_threads();
    openmp_dynamic = omp_get_dynamic();
#endif
    const bool pass = snapshot.commit_limit_enforced && snapshot.job_user_limit_enforced &&
        snapshot.job_terminate_at_limit &&
        snapshot.commit_limit_bytes == arch::api::contract::windows_worker_commit_mib * mib &&
        snapshot.job_user_limit_100ns > 0 &&
        (snapshot.limit_flags & JOB_OBJECT_LIMIT_PROCESS_TIME) == 0 &&
        openmp_threads == 1 && openmp_dynamic == 0;
    std::cout << "{\"test\":\"" << (startup ? "startup-info" : "info")
              << "\",\"status\":\"" << (pass ? "PASS" : "FAIL") << "\","
              << "\"policy\":\"" << arch::api::contract::windows_preview_resource_policy << "\","
              << "\"commit_limit_bytes\":" << snapshot.commit_limit_bytes << ","
              << "\"job_user_limit_100ns\":" << snapshot.job_user_limit_100ns << ","
              << "\"job_user_time_100ns\":" << snapshot.job_user_time_100ns << ","
              << "\"current_user_time_100ns\":" << current_user << ","
              << "\"flags\":" << snapshot.limit_flags << ","
              << "\"end_of_job_action\":" << snapshot.end_of_job_action << ","
              << "\"commit_limit_enforced\":" << (snapshot.commit_limit_enforced ? "true" : "false") << ","
              << "\"job_user_limit_enforced\":"
              << (snapshot.job_user_limit_enforced ? "true" : "false") << ","
              << "\"job_terminate_at_limit\":"
              << (snapshot.job_terminate_at_limit ? "true" : "false") << ","
              << "\"openmp_threads\":" << openmp_threads << ","
              << "\"openmp_dynamic\":" << openmp_dynamic << "}\n";
    return pass ? 0 : 1;
}

int run_reserve() {
    arch::api::SessionProcessLimits session;
    constexpr std::size_t reserve_bytes = 2ULL * 1024ULL * mib;
    void* region = VirtualAlloc(nullptr, reserve_bytes, MEM_RESERVE, PAGE_NOACCESS);
    const DWORD error = region == nullptr ? GetLastError() : ERROR_SUCCESS;
    bool released = false;
    if (region != nullptr) released = VirtualFree(region, 0, MEM_RELEASE) != 0;
    const bool pass = region != nullptr && released;
    std::cout << "{\"test\":\"reserve2gib\",\"status\":\"" << (pass ? "PASS" : "FAIL") << "\","
              << "\"reserve_bytes\":" << reserve_bytes << ","
              << "\"allocated\":" << (region != nullptr ? "true" : "false") << ","
              << "\"released\":" << (released ? "true" : "false") << ","
              << "\"touched\":false,\"windows_error\":" << error << "}\n";
    return pass ? 0 : 1;
}

int run_commit_over() {
    arch::api::SessionProcessLimits session;
    constexpr std::size_t commit_bytes =
        arch::api::contract::windows_worker_commit_mib * mib + 64ULL * mib;
    void* region = VirtualAlloc(nullptr, commit_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    const DWORD error = region == nullptr ? GetLastError() : ERROR_SUCCESS;
    bool released = false;
    if (region != nullptr) released = VirtualFree(region, 0, MEM_RELEASE) != 0;
    const bool pass = region == nullptr;
    std::cout << "{\"test\":\"commit-over-1gib\",\"status\":\"" << (pass ? "PASS" : "FAIL") << "\","
              << "\"commit_bytes\":" << commit_bytes << ","
              << "\"rejected\":" << (region == nullptr ? "true" : "false") << ","
              << "\"touched\":false,\"windows_error\":" << error << ","
              << "\"unexpected_release\":" << (released ? "true" : "false") << "}\n";
    return pass ? 0 : 1;
}

int run_single_info_child() {
    const bool startup_burn = burn_user_cpu(600, 3000);
    if (!startup_burn) ExitProcess(cpu_fallback_exit);
    arch::api::ApplyInspectionProcessLimits(1);
    arch::api::ApplyInspectionProcessLimits(300);
    if (!burn_user_cpu(20000, 25000)) ExitProcess(cpu_fallback_exit);
    ExitProcess(cpu_fallback_exit);
}

int run_renewal_child() {
    arch::api::SessionProcessLimits session;
    bool burns_ok = true;
    bool controls_valid = true;
    arch::api::WindowsResourceSnapshot last_snapshot;
    for (int request = 0; request < 12; ++request) {
        session.begin_request(1);
        last_snapshot = session.inspect_windows_resource_limits();
        controls_valid = controls_valid && last_snapshot.commit_limit_enforced &&
            last_snapshot.job_user_limit_enforced && last_snapshot.job_terminate_at_limit &&
            last_snapshot.job_user_limit_100ns > last_snapshot.job_user_time_100ns &&
            last_snapshot.job_user_limit_100ns - last_snapshot.job_user_time_100ns <= 10'000'000ULL &&
            last_snapshot.job_user_limit_100ns - last_snapshot.job_user_time_100ns >= 9'000'000ULL &&
            (last_snapshot.limit_flags & JOB_OBJECT_LIMIT_PROCESS_TIME) == 0;
        burns_ok = burns_ok && burn_user_cpu(700, 3000);
    }
    const std::uint64_t total = process_user_ms();
    const bool pass = burns_ok && controls_valid && total > 1000;
    std::cout << "{\"test\":\"renewal\",\"status\":\"" << (pass ? "PASS" : "FAIL") << "\","
              << "\"requests\":12,\"request_budget_seconds\":1,"
              << "\"kernel_total_user_ms\":" << total << ","
              << "\"last_job_user_limit_100ns\":" << last_snapshot.job_user_limit_100ns << ","
              << "\"last_job_user_time_100ns\":" << last_snapshot.job_user_time_100ns << ","
              << "\"controls_valid\":" << (controls_valid ? "true" : "false") << ","
              << "\"survived\":" << (pass ? "true" : "false") << "}\n";
    return pass ? 0 : 1;
}

int run_tighter_parent_memory_child() {
    arch::api::SessionProcessLimits session;
    constexpr std::size_t probe_bytes = 128ULL * mib;
    void* region = VirtualAlloc(nullptr, probe_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    const DWORD error = region == nullptr ? GetLastError() : ERROR_SUCCESS;
    bool released = false;
    if (region != nullptr) released = VirtualFree(region, 0, MEM_RELEASE) != 0;
    const bool pass = region == nullptr;
    std::cout << "{\"test\":\"tighter-parent-memory\",\"status\":\""
              << (pass ? "PASS" : "FAIL") << "\","
              << "\"probe_bytes\":" << probe_bytes << ","
              << "\"rejected\":" << (region == nullptr ? "true" : "false") << ","
              << "\"touched\":false,\"windows_error\":" << error << ","
              << "\"unexpected_release\":" << (released ? "true" : "false") << "}\n";
    return pass ? 0 : 1;
}

int run_cpu_over_child() {
    arch::api::SessionProcessLimits session;
    session.begin_request(1);
    if (!burn_user_cpu(20000, 25000)) ExitProcess(cpu_fallback_exit);
    ExitProcess(cpu_fallback_exit);
}

int run_tighter_parent_cpu_child() {
    arch::api::SessionProcessLimits session;
    if (!burn_user_cpu(20000, 25000)) ExitProcess(cpu_fallback_exit);
    ExitProcess(cpu_fallback_exit);
}

int run_child_mode(const std::string& mode) {
    if (mode == "single-info") return run_single_info_child();
    if (mode == "renewal") return run_renewal_child();
    if (mode == "tighter-parent-memory") return run_tighter_parent_memory_child();
    if (mode == "cpu-over") return run_cpu_over_child();
    if (mode == "tighter-parent-cpu") return run_tighter_parent_cpu_child();
    throw std::invalid_argument("Unknown child mode: " + mode);
}

int run_contained_mode(const std::string& mode) {
    std::uint64_t parent_memory = 1024 * mib;
    std::uint64_t parent_cpu_seconds = 300;
    if (mode == "tighter-parent-memory") parent_memory = 64 * mib;
    if (mode == "tighter-parent-cpu") parent_cpu_seconds = 1;
    const ChildResult child = run_contained_child(mode, parent_memory, parent_cpu_seconds, 30000);
    const bool expects_cpu_kill = mode == "single-info" || mode == "cpu-over" ||
        mode == "tighter-parent-cpu";
    bool pass = false;
    if (expects_cpu_kill) {
        const bool bounded = child.elapsed_ms < 28000;
        const bool consumed_user_cpu = child.job.total_user_time_100ns >= 700 * ticks_per_ms;
        pass = !child.timed_out && child.elapsed_ms >= 500 && bounded &&
            child.exit_code != 0 && child.exit_code != cpu_fallback_exit &&
            child.output.empty() && consumed_user_cpu;
    } else {
        pass = child_passed(child);
    }
    std::cout << "{\"test\":\"" << mode << "\",\"status\":\"" << (pass ? "PASS" : "FAIL")
              << "\"";
    print_child_job(child);
    if (expects_cpu_kill) {
        std::cout << ",\"expected_job_cpu_kill\":true"
                  << ",\"job_cpu_kill_not_fallback\":"
                  << (child.exit_code != 0 && child.exit_code != cpu_fallback_exit ? "true" : "false");
        std::cout << ",\"child_output\":\"" << json_escape(child.output) << "\"";
    } else {
        std::cout << ",\"child\":" << child_json(child);
    }
    std::cout << "}\n";
    return pass ? 0 : 1;
}

int run_mode(const std::string& mode) {
    if (mode == "info") return run_info();
    if (mode == "startup-info") return run_info(true);
    if (mode == "reserve2gib") return run_reserve();
    if (mode == "commit-over-1gib") return run_commit_over();
    if (mode == "single-info" || mode == "renewal" || mode == "cpu-over" ||
        mode == "tighter-parent-memory" || mode == "tighter-parent-cpu")
        return run_contained_mode(mode);
    throw std::invalid_argument("Unknown mode: " + mode);
}
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    const std::string test = argc > 1 ? argv[1] : "info";
    try {
        if (argc > 2 && test == "--child") return run_child_mode(argv[2]);
        if (argc < 2) {
            print_error("usage", "Expected info, startup-info, reserve2gib, commit-over-1gib, single-info, "
                "cpu-over, renewal, tighter-parent-memory, or tighter-parent-cpu");
            return 2;
        }
        return run_mode(test);
    } catch (const std::exception& error) {
        print_error(test, error.what());
        return 2;
    }
#else
    const std::string test = argc > 1 ? argv[1] : "info";
    std::cout << "{\"test\":\"" << json_escape(test)
              << "\",\"status\":\"UNSUPPORTED\",\"platform\":\"non-Windows\","
              << "\"message\":\"Native Windows resource tests were not executed\"}\n";
    return 3;
#endif
}
