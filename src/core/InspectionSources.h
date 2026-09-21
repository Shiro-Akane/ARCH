#pragma once
#include <functional>
#include <map>
#include <stdexcept>
#include <string>

namespace arch::core {
// A read-only request uses one source generation. The first fingerprint and
// final verification are full content reads, never timestamp/size shortcuts.
// Initial loader before/after checks still run outside this memoization.
class InspectionSources {
    struct Entry { std::string fingerprint; std::function<std::string()> read; };
    std::map<std::string, Entry> sources_;
public:
    std::string fingerprint(const std::string& key, std::function<std::string()> read) {
        if (const auto it = sources_.find(key); it != sources_.end()) return it->second.fingerprint;
        const auto value = read();
        sources_.emplace(key, Entry{value, std::move(read)});
        return value;
    }
    void validate() const {
        for (const auto& [key, source] : sources_)
            if (source.read() != source.fingerprint)
                throw std::runtime_error("EOS source changed during initialization inspection: " + key);
    }
};
} // namespace arch::core
