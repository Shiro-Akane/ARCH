// Directory identity shared by the Windows file publisher and process leases.
#pragma once
#ifdef _WIN32
#include <windows.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace arch::core::windows {
class DirectoryPin {
    HANDLE value_;
public:
    explicit DirectoryPin(HANDLE value) : value_(value) {}
    DirectoryPin(const DirectoryPin&) = delete;
    DirectoryPin& operator=(const DirectoryPin&) = delete;
    DirectoryPin(DirectoryPin&& other) noexcept : value_(other.value_) {
        other.value_ = INVALID_HANDLE_VALUE;
    }
    ~DirectoryPin() { if(value_!=INVALID_HANDLE_VALUE) CloseHandle(value_); }
    HANDLE get() const { return value_; }
};

// Hold every non-reparse ancestor against write/delete rebinding. Inheritable
// pins enter Core only through the supervisor's explicit handle allowlist.
inline std::vector<DirectoryPin> pinDirectories(std::wstring& directory, bool inherit=false) {
    std::replace(directory.begin(),directory.end(),L'/',L'\\');
    if(directory.size()<=3 || directory[1]!=L':' || directory[2]!=L'\\' ||
       !((directory[0]>=L'A'&&directory[0]<=L'Z')||(directory[0]>=L'a'&&directory[0]<=L'z')) ||
       directory.find(L':',2)!=std::wstring::npos)
        throw std::runtime_error("Absolute local directory without streams required");
    std::vector<DirectoryPin> result;
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,inherit?TRUE:FALSE};
    std::size_t end=3;
    for(;;) {
        const auto prefix=directory.substr(0,end);
        const auto value=CreateFileW(prefix.c_str(),FILE_READ_ATTRIBUTES|FILE_LIST_DIRECTORY|FILE_TRAVERSE,
            FILE_SHARE_READ,&security,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot pin directory");
        DirectoryPin pin(value);
        FILE_ATTRIBUTE_TAG_INFO info{};
        if(!GetFileInformationByHandleEx(value,FileAttributeTagInfo,&info,sizeof(info)) ||
           !(info.FileAttributes&FILE_ATTRIBUTE_DIRECTORY) || (info.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Reparse or nondirectory ancestor rejected");
        result.push_back(std::move(pin));
        if(end==directory.size())return result;
        const auto start=end==3?3:end+1;
        end=directory.find(L'\\',start);
        if(end==std::wstring::npos)end=directory.size();
        const auto part=directory.substr(start,end-start);
        if(part.empty() || part==L"." || part==L".." || part.back()==L'.' || part.back()==L' ' ||
           part.find_first_of(L"<>\"|?*\r\n\t")!=std::wstring::npos)
            throw std::runtime_error("Invalid directory component");
    }
}
}
#endif
