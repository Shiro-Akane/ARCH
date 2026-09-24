/**
 * @file PreviewMetadata.h
 * @brief Carry public metadata for case preview without exposing concrete solver owners.
 *
 * Workflow:
 * 1. Receive a resolved request at the owning module boundary.
 * 2. Carry public metadata for case preview without exposing concrete solver owners.
 * 3. Return bounded data through the established interface.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

struct PointCoords;
struct PrimitiveData;

namespace arch::preview {
using ParameterValue = std::variant<double, std::int64_t, bool, std::string>;

/** Promote an observed case value to the stable metadata variant. */
template<class T> ParameterValue parameter_value(const T &value) {
    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, bool>) return value;
    else if constexpr (std::is_integral_v<T>) return std::int64_t(value);
    else return double(value);
}
/** Return the schema type of a case parameter read. */
template<class T> const char *parameter_type() {
    if constexpr (std::is_same_v<T, std::string>) return "string";
    else if constexpr (std::is_same_v<T, bool>) return "bool";
    else if constexpr (std::is_integral_v<T>) return "int";
    else return "float";
}

struct ParameterRead {
    std::string key, type;
    ParameterValue effective_value, default_value;
    std::optional<ParameterValue> explicit_value;
    std::optional<std::string> raw_value;
    std::string source, reason;
    bool ambiguous = false;
    std::size_t read_count = 1;
};

// Opt-in, per-configuration observation. Ordinary simulation has no observer.
// Input snapshots come from the existing parser; this does not parse tokens.
class ParameterReadTrace {
    std::set<std::string> keys_;
    bool observe_all_ = false;
    std::map<std::string, std::string> raw_, strings_;
    std::map<std::string, double> numeric_;
    std::map<std::string, ParameterRead> reads_;
public:
    explicit ParameterReadTrace(std::set<std::string> keys, bool observe_all = false)
        : keys_(std::move(keys)), observe_all_(observe_all) {}
    struct UnitEvidence { std::string unit, basis; bool conflict = false; };
    std::map<std::string, UnitEvidence> units;
    /** Retain an explicit unit claim and flag conflicting claims. */
    void record_unit(const std::string& key, const std::string& unit, const std::string& basis) {
        if (!observe_all_ && !keys_.contains(key)) return;
        auto [it, added] = units.emplace(key, UnitEvidence{unit, basis, false});
        if (!added && it->second.unit != unit) it->second.conflict = true;
    }
    /** Check exact integer spelling and representability of an observed value. */
    template<class T> void validate_numeric(const std::string& key, double number) const {
        if constexpr (std::is_integral_v<T>) {
            if (auto it = raw_.find(key); it != raw_.end()) {
                const auto& token = it->second;
                const auto first = !token.empty() && (token.front() == '+' || token.front() == '-') ? 1u : 0u;
                bool whole = first < token.size();
                for (std::size_t i = first; i < token.size(); ++i) whole &= token[i] >= '0' && token[i] <= '9';
                if (!whole) throw std::invalid_argument("Parameter '"+key+"' requires an integer token");
            }
            if (!std::isfinite(number) || std::trunc(number) != number
                || static_cast<long double>(number) < std::numeric_limits<T>::lowest()
                || static_cast<long double>(number) > std::numeric_limits<T>::max())
                throw std::invalid_argument("Parameter '"+key+"' is not representable as the requested integer/boolean type");
        }
    }
    /** Expose the captured parser tokens for provenance reporting. */
    const auto& raw_input() const { return raw_; }
    /** Snapshot only requested parser tokens before case Setup runs. */
    void capture_input(const std::map<std::string, std::string> &raw,
                       const std::map<std::string, double> &numeric,
                       const std::map<std::string, std::string> &strings) {
        if (observe_all_) { raw_ = raw; numeric_ = numeric; strings_ = strings; return; }
        for (const auto &key : keys_) {
            if (auto it = raw.find(key); it != raw.end()) raw_[key] = it->second;
            if (auto it = numeric.find(key); it != numeric.end()) numeric_[key] = it->second;
            if (auto it = strings.find(key); it != strings.end()) strings_[key] = it->second;
        }
    }
    /** Record the effective, default and explicit value of one case read. */
    template<class T> void observe(const std::string &key, T fallback, T value, bool found) {
        if (!observe_all_ && !keys_.contains(key)) return;
        ParameterRead read{key, parameter_type<T>(), parameter_value(value), parameter_value(fallback),
                           std::nullopt, std::nullopt, "unknown", "untracked-value"};
        if (auto it = raw_.find(key); it != raw_.end()) read.raw_value = it->second;
        if constexpr (std::is_same_v<T, std::string>) {
            if (auto it = strings_.find(key); it != strings_.end())
                read.explicit_value = parameter_value(it->second);
        } else {
            if (auto it = numeric_.find(key); it != numeric_.end()) {
                validate_numeric<T>(key, it->second);
                read.explicit_value = parameter_value(static_cast<T>(it->second));
            }
        }
        if (found && read.explicit_value == std::optional<ParameterValue>(read.effective_value)) {
            read.source = "explicit";
            read.reason.clear();
        } else if (!found && !read.explicit_value) {
            read.source = "default";
            read.reason = read.raw_value ? "parse-failure" : "missing-key";
        }
        auto [it, inserted] = reads_.emplace(key, read);
        if (!inserted) {
            auto &previous = it->second;
            ++previous.read_count;
            previous.ambiguous |= previous.type != read.type
                || previous.effective_value != read.effective_value
                || previous.default_value != read.default_value
                || previous.explicit_value != read.explicit_value
                || previous.source != read.source || previous.reason != read.reason;
        }
    }
    /** Expose observed parameter reads to the API serializer. */
    const auto &reads() const { return reads_; }
};

// Observation at the actual Init boundary, without expression provenance.
struct InitializationObserver {
    virtual ~InitializationObserver() = default;
    virtual void initial_primitive(const PointCoords&, const PrimitiveData&) = 0;
};

// A model publishes the relation to its initialized state. The UI never infers
// it from parameter spelling. Bounds can also be used by the model's validator.
struct AxisPosition {
    std::string id, parameter_key, axis;
    double coordinate, min, max;
    bool min_inclusive = false, max_inclusive = false;
    /** Test a coordinate against the declared inclusive/exclusive bound. */
    bool outside(double value) const {
        return (min_inclusive ? value < min : value <= min)
            || (max_inclusive ? value > max : value >= max);
    }
};
} // namespace arch::preview
