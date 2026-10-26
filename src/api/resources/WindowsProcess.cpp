/**
 * Windows process primitives for the existing Studio runners, not a workflow.
 * Hold a verified parent handle, create the target inside a private Job before
 * its first instruction, and inherit only standard streams. Closing the sole
 * Job handle reclaims descendants even if this supervisor is forcibly killed.
 * No scientific limits are translated here: commit is not RLIMIT_AS.
 */
#include <windows.h>
#include "../../core/files/WindowsDirectoryPins.h"
#include <aclapi.h>
#include <knownfolders.h>
#include <objbase.h>
#include <shlobj.h>
#include <sddl.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include <array>
#include <cstdint>
#include <cwctype>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;
using Descriptor = std::unique_ptr<void, decltype(&LocalFree)>;
[[noreturn]] void fail(const char* action);
[[noreturn]] void fail(const char* action) {
    throw std::runtime_error(std::string(action) + " (Win32 " +
                             std::to_string(GetLastError()) + ")");
}
Handle checked(HANDLE value, const char* action) {
    if (!value || value == INVALID_HANDLE_VALUE) fail(action);
    return Handle(value, CloseHandle);
}
std::uint64_t number(const wchar_t* text) {
    const std::wstring token(text);
    if (token.empty() || token.find_first_not_of(L"0123456789") != token.npos)
        throw std::runtime_error("Invalid process identity");
    return std::stoull(token);
}
DWORD pid(const wchar_t* text) {
    const auto value = number(text);
    if (!value || value > std::numeric_limits<DWORD>::max())
        throw std::runtime_error("Invalid PID");
    return static_cast<DWORD>(value);
}
std::uint64_t creation(HANDLE process) {
    FILETIME started{}, ended{}, kernel{}, user{};
    if (!GetProcessTimes(process, &started, &ended, &kernel, &user))
        fail("Query process creation time");
    return (std::uint64_t(started.dwHighDateTime) << 32) | started.dwLowDateTime;
}
Handle process_handle(DWORD id) {
    return checked(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
                               FALSE, id), "Open process identity");
}
std::vector<unsigned char> current_user_sid() {
    HANDLE raw = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
        fail("Open current process token");
    Handle token(raw, CloseHandle);
    DWORD bytes = 0;
    if (GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        fail("Query current user identity");
    std::vector<unsigned char> information(bytes);
    if (!GetTokenInformation(token.get(), TokenUser, information.data(), bytes, &bytes))
        fail("Read current user identity");
    const auto* user = reinterpret_cast<const TOKEN_USER*>(information.data());
    const DWORD sid_size = GetLengthSid(user->User.Sid);
    std::vector<unsigned char> sid(sid_size);
    if (!CopySid(sid_size, sid.data(), user->User.Sid))
        fail("Copy current user identity");
    return sid;
}
std::vector<unsigned char> system_sid() {
    DWORD size = SECURITY_MAX_SID_SIZE;
    std::vector<unsigned char> sid(size);
    if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, sid.data(), &size))
        fail("Build SYSTEM identity");
    sid.resize(size);
    return sid;
}
bool private_owner(HANDLE object) {
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR raw = nullptr;
    const auto status = GetSecurityInfo(object, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &raw);
    if (status != ERROR_SUCCESS) fail("Inspect lock owner");
    Descriptor descriptor(raw, LocalFree);
    if (!owner) throw std::runtime_error("Run output lock owner is unavailable");
    auto user = current_user_sid();
    auto system = system_sid();
    return EqualSid(owner, user.data()) || EqualSid(owner, system.data());
}
bool alive(HANDLE process) {
    const auto state = WaitForSingleObject(process, 0);
    if (state == WAIT_FAILED) fail("Query process liveness");
    return state == WAIT_TIMEOUT;
}
// Microsoft CRT argv quoting, including empty tokens, quotes and trailing '\'.
// lpApplicationName is supplied separately; no command interpreter is involved.
std::wstring quoted(const std::wstring& value) {
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto c : value) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'"' ? 2 * slashes + 1 : slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(2 * slashes, L'\\');
    return result + L'"';
}
Handle standard_handle(DWORD id, bool input) {
    HANDLE duplicate = nullptr;
    const auto source = GetStdHandle(id);
    if (source && source != INVALID_HANDLE_VALUE) {
        if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(),
                             &duplicate, 0, TRUE, DUPLICATE_SAME_ACCESS))
            fail("Duplicate standard stream");
    } else {
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        duplicate = CreateFileW(L"NUL", input ? GENERIC_READ : GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
    }
    return checked(duplicate, "Open standard stream");
}
Descriptor private_descriptor(bool directory) {
    auto user = current_user_sid();
    LPWSTR raw_sid = nullptr;
    if (!ConvertSidToStringSidW(user.data(), &raw_sid)) fail("Format current user identity");
    std::unique_ptr<wchar_t, decltype(&LocalFree)> sid_text(raw_sid, LocalFree);
    // D:P removes inherited ACEs. The owner is the verified current user; SYSTEM
    // is retained for normal Windows service/cleanup paths.
    const std::wstring flags=directory?L"OICI":L"";
    std::wstring sddl(L"D:P(A;");
    sddl.append(flags).append(L";FA;;;SY)(A;");
    sddl.append(flags).append(L";FA;;;").append(sid_text.get()).append(L")");
    PSECURITY_DESCRIPTOR raw = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &raw, nullptr)) fail("Build private lock DACL");
    return Descriptor(raw, LocalFree);
}
// Reject inherited or foreign ACEs instead of assuming a created object kept the
// DACL requested at creation. The owner must be this user or SYSTEM.
void require_private(HANDLE object) {
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR raw = nullptr;
    const auto status = GetSecurityInfo(object, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner, nullptr, &dacl, nullptr, &raw);
    if (status != ERROR_SUCCESS) fail("Inspect private lock security");
    Descriptor descriptor(raw, LocalFree);
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (!GetSecurityDescriptorControl(raw, &control, &revision) ||
        !(control & SE_DACL_PROTECTED) || !dacl)
        throw std::runtime_error("Run output lock ACL is not private");
    if (!owner) throw std::runtime_error("Run output lock owner is unavailable");
    auto user = current_user_sid();
    auto system = system_sid();
    if (!EqualSid(owner, user.data()) && !EqualSid(owner, system.data()))
        throw std::runtime_error("Run output lock owner is not private");
    ACL_SIZE_INFORMATION size{};
    if (!GetAclInformation(dacl, &size, sizeof(size), AclSizeInformation) || !size.AceCount)
        throw std::runtime_error("Run output lock ACL is empty");
    for (DWORD index = 0; index < size.AceCount; ++index) {
        void* raw_ace = nullptr;
        if (!GetAce(dacl, index, &raw_ace)) fail("Read private lock ACL");
        const auto* ace = static_cast<const ACE_HEADER*>(raw_ace);
        if (ace->AceType != ACCESS_ALLOWED_ACE_TYPE)
            throw std::runtime_error("Run output lock ACL contains a foreign ACE");
        const auto& allowed = *reinterpret_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
        PSID sid = reinterpret_cast<PSID>(const_cast<DWORD*>(&allowed.SidStart));
        if (!EqualSid(sid, user.data()) && !EqualSid(sid, system.data()))
            throw std::runtime_error("Run output lock ACL grants a foreign identity");
    }
}
std::wstring known_local_appdata() {
    PWSTR raw = nullptr;
    const auto status = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT,
                                             nullptr, &raw);
    if (status != S_OK || !raw) fail("Locate current user LocalAppData");
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> value(raw, CoTaskMemFree);
    std::wstring path(value.get());
    if (path.size() < 3 || path[1] != L':' || path.find(L'\\') == path.npos)
        throw std::runtime_error("LocalAppData is not an absolute local path");
    return path;
}
void validate_directory(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info)) fail("Inspect run output lock directory");
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        throw std::runtime_error("Unsafe run output lock directory");
    if (!private_owner(handle))
        throw std::runtime_error("Run output lock directory owner is not private");
}
// Read-only pinning keeps concurrent runs able to traverse the shared namespace
// while denying rename/delete of each pinned component.
Handle pinned_directory(const std::wstring& path, DWORD share = FILE_SHARE_READ) {
    auto handle = checked(CreateFileW(path.c_str(), FILE_LIST_DIRECTORY | READ_CONTROL,
        share, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr),
        "Open run output lock directory");
    validate_directory(handle.get());
    return handle;
}
std::wstring lock_directory() {
    // Environment TEMP/TMP is per-Host configurable and must never split one
    // user's lock namespace. FOLDERID_LocalAppData is stable for the account.
    return known_local_appdata();
}
std::wstring lock_key(const wchar_t* text) {
    const std::wstring key(text ? text : L"");
    if (key.size() != 64 || key.find_first_not_of(L"0123456789abcdef") != key.npos)
        throw std::runtime_error("Invalid output lock key");
    return key;
}
struct LockSet {
    Descriptor directory_security;
    Descriptor file_security;
    // Held from LocalAppData to the lock directory. FILE_SHARE_READ denies
    // rename/delete of every pinned ancestor, so the path cannot be rebound
    // between opening the directory and opening each lock file.
    std::vector<arch::core::windows::DirectoryPin> ancestors;
    std::vector<Handle> handles;
    explicit LockSet(const std::vector<std::wstring>& keys)
        : directory_security(nullptr,LocalFree), file_security(nullptr,LocalFree) {
        auto ordered = keys;
        std::sort(ordered.begin(), ordered.end());
        ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
        if (ordered.empty()) return;
        directory_security=private_descriptor(true);
        file_security=private_descriptor(false);
        auto local = lock_directory();
        ancestors=arch::core::windows::pinDirectories(local,true);
        SECURITY_ATTRIBUTES directory_attributes{sizeof(directory_attributes),directory_security.get(),FALSE};
        auto directory=local+L"\\ARCHStudioRunLocks-v1";
        if(!CreateDirectoryW(directory.c_str(),&directory_attributes) && GetLastError()!=ERROR_ALREADY_EXISTS)
            fail("Create private lock directory");
        auto check=pinned_directory(directory);
        require_private(check.get());
        // Keep the shared namespace pinned, including after supervisor death.
        ancestors=arch::core::windows::pinDirectories(directory,true);
        // No deletion: every participant opens the same stable name.
        for (const auto& key : ordered) {
            SECURITY_ATTRIBUTES file_attributes{sizeof(file_attributes),
                                                file_security.get(), TRUE};
            const auto path = directory + L"\\" + key + L".lock";
            const auto value = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE |
                READ_CONTROL | WRITE_DAC, 0, &file_attributes, OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (value == INVALID_HANDLE_VALUE) {
                const auto error = GetLastError();
                if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION)
                    throw std::runtime_error("Output directory is reserved by another active Studio run");
                fail("Open output lock");
            }
            auto file = checked(value, "Own output lock");
            BY_HANDLE_FILE_INFORMATION file_info{};
            if (!GetFileInformationByHandle(file.get(), &file_info)) fail("Inspect output lock");
            if ((file_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                (file_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                file_info.nNumberOfLinks != 1)
                throw std::runtime_error("Unsafe run output lock file");
            if (!private_owner(file.get()))
                throw std::runtime_error("Run output lock owner is not private");
            require_private(file.get());
            handles.push_back(std::move(file));
        }
    }
};
struct Attributes {
    std::vector<unsigned char> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    Attributes() {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 2, 0, &bytes);
        storage.resize(bytes);
        list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(list, 2, 0, &bytes))
            fail("Initialize process attributes");
    }
    ~Attributes() { DeleteProcThreadAttributeList(list); }
    void set(DWORD_PTR key, void* value, SIZE_T size) {
        if (!UpdateProcThreadAttribute(list, 0, key, value, size, nullptr, nullptr))
            fail("Set process attributes");
    }
};
std::wstring absolute_executable(const wchar_t* value) {
    const std::wstring executable(value ? value : L"");
    if (executable.size() < 4 || executable[1] != L':' ||
        (executable[2] != L'\\' && executable[2] != L'/') ||
        !((executable[0] >= L'A' && executable[0] <= L'Z') ||
          (executable[0] >= L'a' && executable[0] <= L'z')))
        throw std::runtime_error("An absolute local Windows executable is required");
    if (executable.size() < 4 || executable[executable.size() - 4] != L'.' ||
        std::towlower(executable[executable.size() - 3]) != L'e' ||
        std::towlower(executable[executable.size() - 2]) != L'x' ||
        std::towlower(executable[executable.size() - 1]) != L'e')
        throw std::runtime_error("A local Windows .exe is required");
    return executable;
}
std::wstring command_line(int first, int argc, wchar_t** argv) {
    std::wstring command;
    for (int i = first; i < argc; ++i) {
        if (i != first) command += L' ';
        command += quoted(argv[i]);
    }
    return command;
}
void terminate_and_drain(HANDLE job) {
    // Cancellation and normal leader exit both reap descendants before inherited
    // output-lock handles are allowed to close.
    if (!TerminateJobObject(job, 125)) fail("Reap remaining descendants");
    const auto deadline = GetTickCount64() + 10000;
    for (;;) {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
                                       &accounting, sizeof(accounting), nullptr))
            fail("Query Job completion");
        if (!accounting.ActiveProcesses) return;
        if (GetTickCount64() >= deadline) throw std::runtime_error("Job did not drain");
        Sleep(10);
    }
}
// Production Run owns a separate cancellation pipe; Core receives NUL stdin.
// Polling the one-reader pipe avoids a blocking read during destructor/join.
class RunCancellation {
    std::atomic<bool> finished_{false};
    Handle event_{nullptr,CloseHandle};
    std::thread reader_;
public:
    explicit RunCancellation(bool enabled) {
        if(!enabled)return;
        event_=checked(CreateEventW(nullptr,TRUE,FALSE,nullptr),"Create Run cancellation event");
        reader_=std::thread([this]{
            const auto pipe=GetStdHandle(STD_INPUT_HANDLE);
            while(!finished_.load()) {
                DWORD available=0;
                if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr))return;
                if(available) {
                    char token=0;DWORD count=0;
                    if(ReadFile(pipe,&token,1,&count,nullptr)&&count==1&&token=='!')SetEvent(event_.get());
                    return;
                }
                Sleep(10);
            }
        });
    }
    ~RunCancellation(){finished_=true;if(reader_.joinable())reader_.join();}
    HANDLE event()const{return event_.get();}
};
Handle null_input() {
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    return checked(CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
        &security,OPEN_EXISTING,0,nullptr),"Open Core null input");
}
DWORD supervise(int argc, wchar_t** argv) {
    if (argc < 6)
        throw std::runtime_error("Use run PARENT_PID CREATION_TIME [--lock-key KEY]... -- ABSOLUTE_EXE [args]");
    const std::wstring first_option(argv[4]);
    if (first_option != L"--" && first_option != L"--lock-key" && first_option != L"--cancel-stdin")
        throw std::runtime_error("Use run PARENT_PID CREATION_TIME [--lock-key KEY]... -- ABSOLUTE_EXE [args]");
    int cursor = 4;
    std::vector<std::wstring> keys;
    bool controlled=false;
    while(cursor<argc && std::wstring_view(argv[cursor])!=L"--") {
        const std::wstring_view option(argv[cursor++]);
        if(option==L"--lock-key" && cursor<argc)keys.push_back(lock_key(argv[cursor++]));
        else if(option==L"--cancel-stdin" && !controlled)controlled=true;
        else throw std::runtime_error("Invalid supervisor option");
    }
    if (cursor >= argc || std::wstring(argv[cursor]) != L"--" || cursor + 1 >= argc)
        throw std::runtime_error("Use run PARENT_PID CREATION_TIME [--lock-key KEY]... -- ABSOLUTE_EXE [args]");
    auto parent = process_handle(pid(argv[2]));
    if (creation(parent.get()) != number(argv[3]) || !alive(parent.get()))
        throw std::runtime_error("Parent process identity unavailable or changed");
    const std::wstring executable = absolute_executable(argv[cursor + 1]);
    std::wstring command = command_line(cursor + 1, argc, argv);
    if(keys.size()>16)throw std::runtime_error("Too many output leases");
    // Acquire every exclusive lease before Core exists. These handles are inherited
    // into Core and stay open through helper death until Core itself exits.
    LockSet locks(keys);
    // Default security attributes make Job/parent handles non-inheritable.
    auto job = checked(CreateJobObjectW(nullptr, nullptr), "Create private Job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                                  &limits, sizeof(limits))) fail("Set Job ownership");
    auto input = controlled?null_input():standard_handle(STD_INPUT_HANDLE,true);
    RunCancellation cancellation(controlled);
    auto output = standard_handle(STD_OUTPUT_HANDLE, false);
    auto error = standard_handle(STD_ERROR_HANDLE, false);
    std::vector<HANDLE> inherited{input.get(), output.get(), error.get()};
    for (const auto& lock : locks.handles) inherited.push_back(lock.get());
    for (const auto& pin : locks.ancestors) inherited.push_back(pin.get());
    HANDLE job_handle = job.get();
    Attributes attributes;
    attributes.set(PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(),
                   inherited.size() * sizeof(HANDLE));
    // Job-at-creation removes the crash window of create -> assign. Unsupported
    // systems fail here; there is no uncontained launch fallback.
    attributes.set(PROC_THREAD_ATTRIBUTE_JOB_LIST, &job_handle, sizeof(job_handle));
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input.get();
    startup.StartupInfo.hStdOutput = output.get();
    startup.StartupInfo.hStdError = error.get();
    startup.lpAttributeList = attributes.list;
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
            &startup.StartupInfo, &child)) fail("Create contained child");
    auto process = checked(child.hProcess, "Own child process");
    auto thread = checked(child.hThread, "Own child thread");
    if (!alive(parent.get())) throw std::runtime_error("Parent exited before launch");
    if (ResumeThread(thread.get()) == DWORD(-1)) fail("Resume contained child");
    std::vector<HANDLE> waiting{parent.get(),process.get()};
    if(controlled)waiting.push_back(cancellation.event());
    const auto event = WaitForMultipleObjects(waiting.size(), waiting.data(), FALSE, INFINITE);
    if(controlled && event==WAIT_OBJECT_0+2) {terminate_and_drain(job.get());return 130;}
    if (event == WAIT_OBJECT_0) {
        terminate_and_drain(job.get());
        throw std::runtime_error("Parent exited; contained process tree was reaped");
    }
    if (event != WAIT_OBJECT_0 + 1)
        throw std::runtime_error("Process supervisor wait failed");
    DWORD code = 0;
    if (!GetExitCodeProcess(process.get(), &code)) fail("Read child exit status");
    // Completion includes cleanup of descendants that outlived the leader.
    terminate_and_drain(job.get());
    return code;
}
void private_run_directory(const wchar_t* root_value,const wchar_t* id_value) {
    const std::wstring id(id_value);
    if(id.size()!=36 || id.find_first_not_of(L"0123456789abcdef-")!=std::wstring::npos ||
       id[8]!=L'-'||id[13]!=L'-'||id[18]!=L'-'||id[23]!=L'-')
        throw std::runtime_error("Invalid run directory identity");
    std::wstring parent=std::wstring(root_value)+L"\\studio\\.local\\runs";
    auto pins=arch::core::windows::pinDirectories(parent);
    auto descriptor=private_descriptor(true);
    SECURITY_ATTRIBUTES security{sizeof(security),descriptor.get(),FALSE};
    const auto target=parent+L"\\"+id;
    if(!CreateDirectoryW(target.c_str(),&security))fail("Create private run directory");
    auto directory=pinned_directory(target);require_private(directory.get());
}
DWORD independent_console(int argc, wchar_t** argv) {
    if (argc < 4 || std::wstring(argv[2]) != L"--")
        throw std::runtime_error("Use console -- ABSOLUTE_EXE [args]");
    const std::wstring executable = absolute_executable(argv[3]);
    std::wstring command = command_line(3, argc, argv);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    // The worker gets its own console and breaks out of any Host Job. A helper
    // failure is visible on the Host terminal log; it never falls back uncontained.
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
            CREATE_NEW_CONSOLE | CREATE_BREAKAWAY_FROM_JOB, nullptr, nullptr,
            &startup, &child)) fail("Create independent terminal");
    auto process = checked(child.hProcess, "Own independent terminal");
    auto thread = checked(child.hThread, "Own independent terminal thread");
    if (WaitForSingleObject(process.get(), INFINITE) != WAIT_OBJECT_0)
        fail("Wait for independent terminal");
    DWORD code = 0;
    if (!GetExitCodeProcess(process.get(), &code)) fail("Read independent terminal status");
    return code;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const std::wstring_view action=argc>1?argv[1]:L"";
        if(argc==4 && action==L"run-directory") {
            private_run_directory(argv[2],argv[3]);
            std::cout<<"{\"created\":true}\n";return 0;
        }
        if (argc == 3 && action == L"identity") {
            const auto id = pid(argv[2]);
            auto process = process_handle(id);
            std::cout << "{\"pid\":" << id << ",\"creationTime\":\"" << creation(process.get())
                      << "\",\"alive\":" << (alive(process.get()) ? "true" : "false") << "}\n";
            return 0;
        }
        if (argc >= 2 && action == L"run") {
            const auto code = supervise(argc, argv);
            // Preserve the complete DWORD exit status, including exception codes.
            ExitProcess(code);
        }
        if (argc >= 2 && action == L"console") {
            const auto code = independent_console(argc, argv);
            ExitProcess(code);
        }
        throw std::runtime_error("Use identity PID, run ..., or console -- EXE [args]");
    } catch (const std::exception& error) {
        std::cerr << "ARCH process supervisor: " << error.what() << '\n';
        return 125;
    }
}
