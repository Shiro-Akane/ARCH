#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace arch::preview {
using ParameterValue = std::variant<double, std::int64_t, bool, std::string>;

template<class T> ParameterValue parameter_value(const T &value) {
    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, bool>) return value;
    else if constexpr (std::is_integral_v<T>) return std::int64_t(value);
    else return double(value);
}
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
    std::map<std::string, std::string> raw_, strings_;
    std::map<std::string, double> numeric_;
    std::map<std::string, ParameterRead> reads_;
public:
    explicit ParameterReadTrace(std::set<std::string> keys) : keys_(std::move(keys)) {}
    void capture_input(const std::map<std::string, std::string> &raw,
                       const std::map<std::string, double> &numeric,
                       const std::map<std::string, std::string> &strings) {
        for (const auto &key : keys_) {
            if (auto it = raw.find(key); it != raw.end()) raw_[key] = it->second;
            if (auto it = numeric.find(key); it != numeric.end()) numeric_[key] = it->second;
            if (auto it = strings.find(key); it != strings.end()) strings_[key] = it->second;
        }
    }
    template<class T> void observe(const std::string &key, T fallback, T value, bool found) {
        if (!keys_.contains(key)) return;
        ParameterRead read{key, parameter_type<T>(), parameter_value(value), parameter_value(fallback),
                           std::nullopt, std::nullopt, "unknown", "untracked-value"};
        if (auto it = raw_.find(key); it != raw_.end()) read.raw_value = it->second;
        if constexpr (std::is_same_v<T, std::string>) {
            if (auto it = strings_.find(key); it != strings_.end())
                read.explicit_value = parameter_value(it->second);
        } else {
            if (auto it = numeric_.find(key); it != numeric_.end())
                read.explicit_value = parameter_value(static_cast<T>(it->second));
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
    const auto &reads() const { return reads_; }
};

// A model publishes the relation to its initialized state. The UI never infers
// it from parameter spelling. Bounds can also be used by the model's validator.
struct AxisPosition {
    std::string id, parameter_key, axis;
    double coordinate, min, max;
    bool min_inclusive = false, max_inclusive = false;
    bool outside(double value) const {
        return (min_inclusive ? value < min : value <= min)
            || (max_inclusive ? value > max : value >= max);
    }
};
} // namespace arch::preview
