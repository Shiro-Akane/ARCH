#pragma once
#include <cstddef>
#include <map>
#include <memory>
#include <string>

namespace arch::core {
// Optional CPU preview-session optimization for file_sha256. A hit requires a
// complete byte comparison, never stat metadata. Files larger than the budget
// use ordinary streaming SHA-256. Retained byte allocations have a fixed cap.
class VerifiedFileCache {
    struct Entry { std::unique_ptr<char[]> bytes; std::size_t size; std::string digest; };
    std::map<std::string, Entry> entries_;
    std::size_t retained_ = 0;
    friend std::string file_sha256(const std::string&);
public:
    const std::size_t max_bytes;
    std::size_t hits = 0, hashes = 0;
    explicit VerifiedFileCache(std::size_t limit) : max_bytes(limit) {}
    std::size_t retained_bytes() const { return retained_; }
    void clear() { entries_.clear(); retained_=0; }
};
class VerifiedFileCacheScope {
    VerifiedFileCache* previous_;
public:
    explicit VerifiedFileCacheScope(VerifiedFileCache& cache);
    ~VerifiedFileCacheScope();
    VerifiedFileCacheScope(const VerifiedFileCacheScope&) = delete;
    VerifiedFileCacheScope& operator=(const VerifiedFileCacheScope&) = delete;
};
}
