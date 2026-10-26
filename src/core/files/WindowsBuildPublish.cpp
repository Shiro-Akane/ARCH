// Opaque Windows file operations. TypeScript retains each content/schema owner.
// Hold every non-reparse ancestor against write/delete rebinding, and rename the
// exclusively owned file by handle. No scientific file format is implemented.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "WindowsDirectoryPins.h"
#include <winternl.h>
#include <sddl.h>
#include <aclapi.h>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(other.value) { other.value = INVALID_HANDLE_VALUE; }
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    void close() {
#ifdef ARCH_BUILD_PUBLISH_TEST
        static unsigned calls=0;
        ++calls;
        wchar_t failure[64]{};
        GetEnvironmentVariableW(L"ARCH_PUBLISH_TEST_FAIL",failure,64);
        if ((std::wstring(failure)==L"close-api" && calls==1) ||
            (std::wstring(failure)==L"close-api-committed" && calls==2)) {
            SetLastError(ERROR_INVALID_HANDLE);
            throw std::runtime_error("close");
        }
#endif
        if (!CloseHandle(value)) throw std::runtime_error("close");
        value = INVALID_HANDLE_VALUE;
    }
};
void require(bool ok, const char* operation) {
    if (!ok) throw std::runtime_error(operation);
}
bool hex(wchar_t c) { return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'); }
bool targetName(const std::wstring& name) {
    return name.size() == 35 && name.substr(0, 6) == L"build-" &&
        name.substr(30) == L".json" && std::all_of(name.begin()+6, name.begin()+30, hex);
}
bool tempName(const std::wstring& name) {
    if (name.size() != 47 || name.substr(0, 7) != L".build-" || name.substr(43) != L".tmp") return false;
    for (std::size_t i=7; i<43; ++i) {
        const bool dash = i==15 || i==20 || i==25 || i==30;
        if (dash ? name[i]!=L'-' : !hex(name[i])) return false;
    }
    return true;
}
// A single safe basename, never a stream, DOS device, alias or path.
bool plainName(const std::wstring& name) {
    if (name.empty() || name.size()>240 || name.back()==L'.' || name.back()==L' ' ||
        name.find_first_of(L"/\\:<>\"|?*\r\n\t")!=std::wstring::npos) return false;
    if (std::any_of(name.begin(),name.end(),[](wchar_t c){return c<32;})) return false;
    auto stem=name.substr(0,name.find(L'.'));
    std::transform(stem.begin(),stem.end(),stem.begin(),[](wchar_t c){return std::towupper(c);});
    return stem!=L"CON" && stem!=L"PRN" && stem!=L"AUX" && stem!=L"NUL" &&
        !(stem.size()==4 && (stem.substr(0,3)==L"COM" || stem.substr(0,3)==L"LPT") &&
          ((stem[3]>=L'0' && stem[3]<=L'9') || stem[3]==L'\u00b9' || stem[3]==L'\u00b2' || stem[3]==L'\u00b3'));
}
bool configTemp(const std::wstring& name) {
    if (name.substr(0,13)!=L".arch-config-") return false;
    return tempName(L".build-"+name.substr(13));
}
void stage(const char* value) {
    std::printf("{\"stage\":\"%s\"}\n",value); std::fflush(stdout);
}
void commitGate() {
    char value=0; DWORD count=0;
    require(ReadFile(GetStdHandle(STD_INPUT_HANDLE),&value,1,&count,nullptr) && count==1 && value=='!', "publication-not-confirmed");
}
std::vector<unsigned char> securityOf(HANDLE file) {
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    const auto result=GetSecurityInfo(file,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|OWNER_SECURITY_INFORMATION,
        nullptr,nullptr,nullptr,nullptr,&descriptor);
    if(result!=ERROR_SUCCESS) {SetLastError(result);throw std::runtime_error("read-file-security");}
    const auto bytes=GetSecurityDescriptorLength(descriptor);
    std::vector<unsigned char> copy(static_cast<unsigned char*>(descriptor),static_cast<unsigned char*>(descriptor)+bytes);
    LocalFree(descriptor); return copy;
}
// Preserve the actual DACL and its inheritance protection, not a chmod analogy.
// Files with a different owner are refused rather than changing owner semantics.
void preserveDacl(HANDLE target, HANDLE temporary) {
    const auto old=securityOf(target), own=securityOf(temporary);
    PSID oldOwner=nullptr,newOwner=nullptr; BOOL defaulted=FALSE;
    require(GetSecurityDescriptorOwner(const_cast<unsigned char*>(old.data()),&oldOwner,&defaulted) &&
        GetSecurityDescriptorOwner(const_cast<unsigned char*>(own.data()),&newOwner,&defaulted) &&
        oldOwner && newOwner && EqualSid(oldOwner,newOwner),"configuration-owner-differs");
    SECURITY_DESCRIPTOR_CONTROL control{}; DWORD revision=0;
    require(GetSecurityDescriptorControl(const_cast<unsigned char*>(old.data()),&control,&revision),"read-dacl-control");
    const auto protection=(control&SE_DACL_PROTECTED)?PROTECTED_DACL_SECURITY_INFORMATION:UNPROTECTED_DACL_SECURITY_INFORMATION;
    PACL originalDacl=nullptr; BOOL present=FALSE;
    require(GetSecurityDescriptorDacl(const_cast<unsigned char*>(old.data()),&present,&originalDacl,&defaulted),"read-original-dacl");
    require(present && originalDacl,"unsupported-null-dacl");
    const auto changed=SetSecurityInfo(temporary,SE_FILE_OBJECT,DACL_SECURITY_INFORMATION|protection,
        nullptr,nullptr,originalDacl,nullptr);
    if(changed!=ERROR_SUCCESS) {SetLastError(changed);throw std::runtime_error("preserve-configuration-dacl");}
    const auto actual=securityOf(temporary);
    PACL before=nullptr,after=nullptr; BOOL beforePresent=FALSE,afterPresent=FALSE;
    SECURITY_DESCRIPTOR_CONTROL actualControl{};
    require(GetSecurityDescriptorDacl(const_cast<unsigned char*>(old.data()),&beforePresent,&before,&defaulted) &&
        GetSecurityDescriptorDacl(const_cast<unsigned char*>(actual.data()),&afterPresent,&after,&defaulted) &&
        GetSecurityDescriptorControl(const_cast<unsigned char*>(actual.data()),&actualControl,&revision),"verify-dacl");
    require(beforePresent==afterPresent && bool(before)==bool(after) &&
        (!before || (before->AclSize==after->AclSize && std::memcmp(before,after,before->AclSize)==0)) &&
        (control&SE_DACL_PROTECTED)==(actualControl&SE_DACL_PROTECTED),"dacl-preservation-mismatch");
}
struct LocalAllocation {
    void* value=nullptr;
    ~LocalAllocation() { if(value) LocalFree(value); }
};
// Match the owner-private Linux temp contract without inheriting a public DACL.
// This is a new file's DACL, not an attempt to emulate all Windows ACL behavior.
void privateSecurity(LocalAllocation& descriptor, SECURITY_ATTRIBUTES& attributes) {
    Handle token;
    require(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value), "process-token");
    DWORD size=0;
    GetTokenInformation(token.value,TokenUser,nullptr,0,&size);
    require(size>0 && size<65536, "token-user-size");
    std::vector<unsigned char> user(size);
    require(GetTokenInformation(token.value,TokenUser,user.data(),size,&size), "token-user");
    LocalAllocation sid;
    require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,
        reinterpret_cast<LPWSTR*>(&sid.value)), "owner-sid");
    const auto sddl=L"D:P(A;;FA;;;SY)(A;;FA;;;"+std::wstring(static_cast<wchar_t*>(sid.value))+L")";
    require(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,
        &descriptor.value,nullptr), "private-file-security");
    attributes={sizeof(SECURITY_ATTRIBUTES),descriptor.value,FALSE};
}
#ifdef ARCH_BUILD_PUBLISH_TEST
// Compiled only into the standalone native test executable, never installed.
void checkpoint(const wchar_t* stage) {
    wchar_t requested[64]{};
    GetEnvironmentVariableW(L"ARCH_PUBLISH_TEST_FAIL", requested, 64);
    if (std::wstring(requested)==stage) throw std::runtime_error("injected-failure");
    wchar_t paused[64]{};
    GetEnvironmentVariableW(L"ARCH_PUBLISH_TEST_PAUSE", paused, 64);
    if (std::wstring(paused)==stage) {
        std::fprintf(stderr,"checkpoint\n"); std::fflush(stderr);
        char byte; DWORD count=0;
        require(ReadFile(GetStdHandle(STD_INPUT_HANDLE),&byte,1,&count,nullptr) && count==1 && byte=='!', "test-gate");
    }
}
#else
void checkpoint(const wchar_t*) {}
#endif
}
int wmain(int argc, wchar_t** argv) {
    bool committed=false;
    Handle writer, identity;
    // Keep pins alive through cleanup on every failure path.
    std::vector<arch::core::windows::DirectoryPin> pins;
    try {
        const std::wstring operation=argc>1?argv[1]:L"";
        if(argc==2 && operation==L"capabilities") {
            std::printf("{\"protocolVersion\":\"windows-file-1\",\"manifest\":true,\"config\":true,\"holdRead\":true}\n");
            return 0;
        }
        const bool save=argc==6 && operation==L"config-save";
        const bool create=argc==6 && operation==L"config-create";
        const bool hold=argc==4 && operation==L"hold-read";
        const bool configuration=save||create;
        require(configuration || hold || argc==4,"arguments");
        std::wstring directory=argv[configuration||hold?2:1],target=argv[configuration||hold?3:2];
        std::wstring temp=hold?L"":argv[configuration?4:3];
        std::size_t length=0;
        if (configuration) {
            const std::wstring number(argv[5]);
            require(!number.empty() && number.size()<9 && number.find_first_not_of(L"0123456789")==std::wstring::npos,"configuration-byte-count");
            length=std::stoul(number);
            require(length<=1024*1024 && plainName(target) && target.size()>4 && target.substr(target.size()-4)==L".par" && configTemp(temp),"configuration-names-or-budget");
        } else if(hold) require(plainName(target),"read-name");
        else require(targetName(target) && tempName(temp),"manifest-names");
        pins=arch::core::windows::pinDirectories(directory);
        checkpoint(L"pinned");
        if(hold) {
            Handle source(CreateFileW((directory+L"\\"+target).c_str(),GENERIC_READ,FILE_SHARE_READ,
                nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
            require(source.value!=INVALID_HANDLE_VALUE,"hold-selected-file");
            FILE_ATTRIBUTE_TAG_INFO info{}; LARGE_INTEGER size{};
            require(GetFileInformationByHandleEx(source.value,FileAttributeTagInfo,&info,sizeof(info)) &&
                !(info.FileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) &&
                GetFileSizeEx(source.value,&size) && size.QuadPart>0 && size.QuadPart<=64*1024*1024,"read-file-admission");
            stage("held"); commitGate(); source.close();
            std::printf("{\"released\":true}\n"); return 0;
        }
        LocalAllocation descriptor; SECURITY_ATTRIBUTES security{};
        privateSecurity(descriptor,security);
        writer.value=CreateFileW((directory+L"\\"+temp).c_str(), GENERIC_WRITE | DELETE | (save ? (READ_CONTROL | WRITE_DAC) : 0), 0,
            &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        require(writer.value!=INVALID_HANDLE_VALUE, "create-exclusive-temp");
        require(DuplicateHandle(GetCurrentProcess(),writer.value,GetCurrentProcess(),&identity.value,0,FALSE,DUPLICATE_SAME_ACCESS), "retain-file-identity");
        checkpoint(L"created");
        if(configuration) stage("created");
        char buffer[65536]; std::size_t total=0;
        for (;;) {
            if(configuration && total==length) break;
            DWORD count=0;
            const DWORD request=configuration?static_cast<DWORD>(std::min<std::size_t>(sizeof(buffer),length-total)):sizeof(buffer);
            const BOOL read=ReadFile(GetStdHandle(STD_INPUT_HANDLE),buffer,request,&count,nullptr);
            if (!read && GetLastError()==ERROR_BROKEN_PIPE) break;
            require(read, "read-input");
            if (!count) break;
            total+=count; require(total<=4*1024*1024, "manifest-byte-budget");
            DWORD offset=0;
            while (offset<count) {
                DWORD written=0;
                require(WriteFile(writer.value,buffer+offset,count-offset,&written,nullptr) && written, "write-temp");
                offset+=written;
            }
            checkpoint(L"write");
        }
        require(configuration?total==length:total>0, "incomplete-input");
        checkpoint(L"flush");
        require(FlushFileBuffers(writer.value), "flush-temp");
        checkpoint(L"close");
        writer.close();
        if(configuration) {
            stage("prepared"); commitGate();
            if(save) {
                Handle previous(CreateFileW((directory+L"\\"+target).c_str(),READ_CONTROL|FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
                require(previous.value!=INVALID_HANDLE_VALUE,"open-original-configuration");
                FILE_ATTRIBUTE_TAG_INFO info{};
                require(GetFileInformationByHandleEx(previous.value,FileAttributeTagInfo,&info,sizeof(info)) &&
                    !(info.FileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_READONLY)),"original-configuration-attributes");
                preserveDacl(previous.value,identity.value); previous.close();
            }
        }
        checkpoint(L"replace");
        // Temp handles remain delete-on-close through all slow/precommit stages.
        // Disarm immediately before rename; after this point a forced process
        // termination has an uncertain publication/cleanup outcome, never rollback.
        FILE_DISPOSITION_INFO_EX keep{FILE_DISPOSITION_FLAG_ON_CLOSE};
        require(SetFileInformationByHandle(identity.value,FileDispositionInfoEx,&keep,sizeof(keep)),"retain-published-file");
        // Use the Native API's same-directory rename by source handle. The
        // Win32 wrapper reopens names/parents and conflicts with the pin. A
        // basename and null root keep the rename in this file's pinned parent.
        const auto bytes=offsetof(FILE_RENAME_INFORMATION,FileName)+(target.size()+1)*sizeof(wchar_t);
        std::vector<unsigned char> storage(bytes);
        auto* rename=reinterpret_cast<FILE_RENAME_INFORMATION*>(storage.data());
        rename->ReplaceIfExists=create?FALSE:TRUE;
        rename->RootDirectory=nullptr;
        rename->FileNameLength=static_cast<DWORD>(target.size()*sizeof(wchar_t));
        std::copy(target.begin(),target.end(),rename->FileName);
        IO_STATUS_BLOCK status{};
        const NTSTATUS result=NtSetInformationFile(identity.value,&status,rename,
            static_cast<ULONG>(bytes),FileRenameInformation);
        if (result<0) {
            SetLastError(RtlNtStatusToDosError(result));
            throw std::runtime_error("replace-manifest");
        }
        committed=true;
        checkpoint(L"committed");
        identity.close();
        std::printf("{\"committed\":true}\n");
        return 0;
    } catch (const std::exception& e) {
        const DWORD error=GetLastError();
        bool cleanup=true;
        if (!committed) {
            const HANDLE owned=identity.value!=INVALID_HANDLE_VALUE?identity.value:writer.value;
            if (owned!=INVALID_HANDLE_VALUE) {
                FILE_DISPOSITION_INFO disposition{TRUE};
                cleanup=SetFileInformationByHandle(owned,FileDispositionInfo,&disposition,sizeof(disposition));
            }
        }
        // Close both aliases before reporting cleanup; no path-based deletion.
        for (Handle* handle : {&writer,&identity}) {
            if (handle->value!=INVALID_HANDLE_VALUE) {
                try { handle->close(); } catch (...) { cleanup=false; }
            }
        }
        std::fprintf(stderr,"Windows file operation failed: %s (win32=%lu, committed=%s, cleanup=%s)\n",
            e.what(),error,committed?"true":"false",cleanup?"true":"false");
        std::printf("{\"committed\":%s,\"cleanup\":%s}\n",committed?"true":"false",cleanup?"true":"false");
        return 1;
    }
}
#endif
