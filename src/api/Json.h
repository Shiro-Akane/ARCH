#pragma once

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace arch::api::detail {
// Output-only JSON values; no second parameter/configuration parser. Non-finite
// metadata is null. Sample arrays are validated before reaching this writer.
class Json {
public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;
    Json() = default;
    Json(bool value) : value_(value) {}
    Json(int value) : value_(std::int64_t(value)) {}
    Json(std::int64_t value) : value_(value) {}
    Json(double value) { if (std::isfinite(value)) value_ = value; }
    Json(const char *value) : value_(std::string(value)) {}
    Json(std::string value) : value_(std::move(value)) {}
    Json(Array value) : value_(std::move(value)) {}
    Json(Object value) : value_(std::move(value)) {}
    static Json object(Object value = {}) { return Json(std::move(value)); }
    static Json array(Array value = {}) { return Json(std::move(value)); }
    Json &operator[](const std::string &key) { return std::get<Object>(value_)[key]; }
    void push(Json value) { std::get<Array>(value_).push_back(std::move(value)); }
    std::string dump() const {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::setprecision(17);
        write(out);
        return out.str();
    }
private:
    std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object> value_;
    static void quote(std::ostream &out, const std::string &text) {
        constexpr char hex[] = "0123456789abcdef";
        out << '"';
        for (unsigned char c : text) {
            if (c == '"' || c == '\\') out << '\\' << char(c);
            else if (c < 0x20) out << "\\u00" << hex[c >> 4] << hex[c & 15];
            else out << char(c);
        }
        out << '"';
    }
    void write(std::ostream &out) const {
        std::visit([&](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) out << "null";
            else if constexpr (std::is_same_v<T, bool>) out << (value ? "true" : "false");
            else if constexpr (std::is_same_v<T, std::string>) quote(out, value);
            else if constexpr (std::is_same_v<T, Array>) {
                out << '['; bool first = true;
                for (const auto &item : value) { if (!first) out << ','; first = false; item.write(out); }
                out << ']';
            } else if constexpr (std::is_same_v<T, Object>) {
                out << '{'; bool first = true;
                for (const auto &[key, item] : value) {
                    if (!first) out << ',';
                    first = false; quote(out, key); out << ':'; item.write(out);
                }
                out << '}';
            } else out << value;
        }, value_);
    }
};
} // namespace arch::api::detail
