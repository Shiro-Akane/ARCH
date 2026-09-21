#pragma once
#include "../data/UserTypes.h"
#include <array>
#include <bit>
#include <cstdint>
#include <map>
#include <vector>

namespace arch::api {
// One field request and one fixed EOS/species view only. Init still runs at
// every coordinate. Reuse only identical binary64 inputs to the pure shared
// conversion; no quantization, interpolation or reuse across configurations.
class InitialSampleCache {
public:
    using Row = std::array<double,7>;
    static constexpr std::size_t max_entries=1024, max_payload_bytes=1024*1024;
    std::size_t conversions=0, hits=0;
private:
    std::map<std::vector<std::uint64_t>,Row> entries_;
    std::size_t payload_bytes_=0;
public:
    template<class Convert>
    Row evaluate(const PrimitiveData& p, Convert&& convert) {
        if (p.mass_fractions.size()>max_payload_bytes/sizeof(std::uint64_t)-8) {
            ++conversions; return convert();
        }
        std::vector<std::uint64_t> key;
        key.reserve(8+p.mass_fractions.size());
        key.push_back(p.has_temperature?1:0);
        for (double value : {p.rho,p.p,p.temperature,p.u,p.v,p.w})
            key.push_back(std::bit_cast<std::uint64_t>(value));
        key.push_back(p.mass_fractions.size());
        for (double value : p.mass_fractions) key.push_back(std::bit_cast<std::uint64_t>(value));
        if (auto it=entries_.find(key);it!=entries_.end()) { ++hits; return it->second; }
        ++conversions;
        Row row=convert(); // Exceptions and invalid values must never be cached.
        const auto bytes=key.size()*sizeof(std::uint64_t)+sizeof(Row);
        if (entries_.size()<max_entries && bytes<=max_payload_bytes-payload_bytes_) {
            entries_.emplace(std::move(key),row); payload_bytes_+=bytes;
        }
        return row;
    }
};
}
