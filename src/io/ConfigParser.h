/**
 * @file ConfigParser.h
 * @brief A lightweight, text-based configuration file parser.
 * Supports simple "key = value" syntax.
 * Handles inline comments (starting with '#') and whitespace trimming.
 * Preserves raw source records and rejects malformed lines and duplicate keys.
 * Typed accessors preserve strict conversion; requirement resolution is a caller responsibility.
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <vector>
#include <iterator>
#include <limits>
#include <type_traits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include "physics/constant/PhysicalConstants.h"

class ConfigValueError : public std::invalid_argument {
public:
    std::string key, code;
    ConfigValueError(std::string parameter, std::string error_code, const std::string& detail)
        : std::invalid_argument("Config parameter '" + parameter + "': " + detail),
          key(std::move(parameter)), code(std::move(error_code)) {}
};

// Byte positions refer to the original UTF-8 line, one-based and end-exclusive.
struct ConfigSourceLocation {
    std::string source;
    std::size_t line, column, end_column;
    std::optional<std::string> raw_value;
};
struct ConfigInputDiagnostic {
    std::string key, code, message;
    std::vector<ConfigSourceLocation> locations;
};
class ConfigInputError : public ConfigValueError {
    static std::string describe(const std::vector<ConfigInputDiagnostic>& errors) {
        std::string text;
        for (const auto& error : errors) {
            if (!text.empty()) text += "; ";
            text += error.code + ": " + error.message;
        }
        return text;
    }
public:
    std::vector<ConfigInputDiagnostic> diagnostics;
    explicit ConfigInputError(const std::vector<ConfigInputDiagnostic>& errors)
        : ConfigValueError(errors.empty() ? "" : errors.front().key,
                           errors.empty() ? "INVALID_INPUT" : errors.front().code, describe(errors)),
          diagnostics(errors) {}
};

class ConfigParser
{
private:
    /// Storage for parsed key-value pairs
    std::map<std::string, std::string> parameters;
    std::map<std::string, std::vector<ConfigSourceLocation>> occurrences;
    std::vector<ConfigInputDiagnostic> diagnostics;
    std::string input_text;

    void reset() {
        parameters.clear();
        occurrences.clear();
        diagnostics.clear();
        input_text.clear();
    }
    void reject_duplicate(const std::string& key) const {
        const auto it = occurrences.find(key);
        if (it != occurrences.end() && it->second.size() > 1)
            throw ConfigInputError({{key, "DUPLICATE_PARAMETER",
                "Duplicate parameter; no occurrence is selected.", it->second}});
    }

    /**
     * @brief Internal helper: Removes leading and trailing whitespace from a string.
     * @param str The input string.
     * @return A new string with no surrounding whitespace.
     */
    std::string trim(const std::string &str)
    {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (std::string::npos == first)
            return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

public:
    static bool ParseBoolean(const std::string& key, std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (text == "true") return true;
        if (text == "false") return false;
        throw ConfigValueError(key, "INVALID_BOOLEAN", "Expected true or false (case-insensitive).");
    }

    // Common production/inspection conversion check for typed programmatic values.
    // Raw tokens, when present, also enforce lexical integer/boolean semantics.
    template<class T>
    static void ValidateNumeric(const std::string& key, double number,
                                const std::string* raw = nullptr) {
        if constexpr (std::is_same_v<T, bool>) {
            if (raw) (void)ParseBoolean(key, *raw);
            if (!std::isfinite(number) || (number != 0.0 && number != 1.0))
                throw ConfigValueError(key, "INVALID_BOOLEAN", "Requires an exact boolean value.");
        } else if constexpr (std::is_integral_v<T>) {
            if (raw) {
                const auto first = !raw->empty() && (raw->front() == '+' || raw->front() == '-') ? 1u : 0u;
                bool whole = first < raw->size();
                for (std::size_t i = first; i < raw->size(); ++i)
                    whole &= (*raw)[i] >= '0' && (*raw)[i] <= '9';
                if (!whole) throw ConfigValueError(key, "INVALID_INTEGER", "Requires an integer token.");
            }
            if (!std::isfinite(number) || std::trunc(number) != number
                || static_cast<long double>(number) < std::numeric_limits<T>::lowest()
                || static_cast<long double>(number) > std::numeric_limits<T>::max())
                throw ConfigValueError(key, "INVALID_INTEGER", "Not representable as the requested integer.");
        } else {
            static_assert(std::is_floating_point_v<T>);
            if (!std::isfinite(number)
                || static_cast<long double>(number) < std::numeric_limits<T>::lowest()
                || static_cast<long double>(number) > std::numeric_limits<T>::max())
                throw ConfigValueError(key, "INVALID_NUMBER", "Requires a finite representable number.");
        }
    }

    static int ParseInteger(const std::string& key, const std::string& text) {
        const char* begin = text.data();
        const char* end = begin + text.size();
        if (begin != end && *begin == '+') ++begin;
        int value = 0;
        const auto result = std::from_chars(begin, end, value);
        if (begin == end || (begin != text.data() && *begin == '-')
            || result.ec != std::errc{} || result.ptr != end)
            throw ConfigValueError(key, "INVALID_INTEGER", "Expected a complete 32-bit integer.");
        return value;
    }

    static double ParseNumber(const std::string& key, const std::string& text) {
        const char* begin = text.data();
        const char* end = begin + text.size();
        if (begin != end && *begin == '+') ++begin;
        double value = 0;
        const auto result = std::from_chars(begin, end, value, std::chars_format::general);
        if (begin == end || (begin != text.data() && *begin == '-')
            || result.ec != std::errc{} || result.ptr != end || !std::isfinite(value))
            throw ConfigValueError(key, "INVALID_NUMBER", "Expected a complete finite decimal or scientific-notation number.");
        return value;
    }

    static double ParseExpression(const std::string& key, std::string text) {
        text.erase(std::remove_if(text.begin(), text.end(),
            [](unsigned char c) { return std::isspace(c); }), text.end());
        constexpr double pi = arch::constants::math::pi;
        double value;
        if (text == "pi") value = pi;
        else if (text == "-pi") value = -pi;
        else if (text.starts_with("pi*")) value = pi * ParseNumber(key, text.substr(3));
        else if (text.starts_with("pi/")) value = pi / ParseNumber(key, text.substr(3));
        else if (text.ends_with("*pi")) value = ParseNumber(key, text.substr(0, text.size()-3)) * pi;
        else if (text.starts_with("exp(") && text.ends_with(")"))
            value = std::exp(ParseNumber(key, text.substr(4, text.size()-5)));
        else value = ParseNumber(key, text);
        if (!std::isfinite(value))
            throw ConfigValueError(key, "INVALID_EXPRESSION", "Expression must produce a finite value.");
        return value;
    }
    /**
     * @brief Loads and parses the specified configuration file.
     * Parsing Rules:
     * 1. Ignores empty lines.
     * 2. Ignores text after '#' (comments).
     * 3. Splits lines by the first '=' character into Key and Value.
     * 4. Trims whitespace around Keys and Values.
     * @param filename Path to the configuration file.
     * @return false when the file cannot be opened; syntax errors throw ConfigInputError.
     */
    bool Load(const std::string &filename)
    {
        reset();
        std::ifstream file(filename, std::ios::binary);
        if (!file.is_open())
        {
            std::cerr << "[Error] Cannot open config file " << filename << std::endl;
            return false;
        }
        return Load(file, filename);
    }

    // Inspection may retain partial records after syntax errors. Runtime Load
    // uses this same reader, then refuses any diagnosed input before resolution.
    void Read(std::istream &input, const std::string &source = "<memory>")
    {
        reset();
        input_text.assign(std::istreambuf_iterator<char>(input), {});
        if (input.bad()) throw std::runtime_error("Cannot read config input: " + source);
        std::istringstream lines(input_text);
        std::string line;
        std::size_t line_number = 0;
        while (std::getline(lines, line))
        {
            ++line_number;
            const auto physical_size = line.size();
            const auto comment = line.find('#');
            if (comment != std::string::npos) line.resize(comment);
            if (trim(line).empty()) continue;
            const auto delimiter = line.find('=');
            if (delimiter == std::string::npos) {
                diagnostics.push_back({"", "MALFORMED_LINE",
                    "Non-comment line requires an equals sign.",
                    {{source, line_number, 1, physical_size + 1, std::nullopt}}});
                continue;
            }
            const auto key = trim(line.substr(0, delimiter));
            if (key.empty()) {
                diagnostics.push_back({"", "EMPTY_KEY", "Parameter key is empty.",
                    {{source, line_number, 1, physical_size + 1, std::nullopt}}});
                continue;
            }
            const auto raw = line.substr(delimiter + 1);
            auto& locations = occurrences[key];
            locations.push_back({source, line_number, delimiter + 2, line.size() + 1, raw});
            if (locations.size() == 1) parameters.emplace(key, trim(raw));
            else parameters.erase(key);
        }
        for (const auto& [key, locations] : occurrences) {
            if (locations.size() > 1)
                diagnostics.push_back({key, "DUPLICATE_PARAMETER",
                    "Duplicate parameter; no occurrence is selected.", locations});
        }
    }

    void ThrowIfInvalid() const {
        if (!diagnostics.empty()) throw ConfigInputError(diagnostics);
    }
    const std::vector<ConfigInputDiagnostic>& Diagnostics() const { return diagnostics; }
    const std::string& InputText() const { return input_text; }
    const std::vector<ConfigSourceLocation>& Locations(const std::string& key) const {
        static const std::vector<ConfigSourceLocation> empty;
        const auto it = occurrences.find(key);
        return it == occurrences.end() ? empty : it->second;
    }

    bool Load(std::istream &input, const std::string &source = "<memory>")
    {
        Read(input, source);
        ThrowIfInvalid();
        std::cout << "[Info] Loaded " << parameters.size()
                  << " parameters from " << source << std::endl;
        return true;
    }

    /**
     * @brief Retrieves a strict, case-insensitive Boolean value.
     * @param key The parameter name.
     * @param defaultVal The value to return if the key is missing.
     * @throws std::invalid_argument if a present value is not true or false.
     */
    bool GetBool(const std::string &key, bool defaultVal) const
    {
        reject_duplicate(key);
        const auto it = parameters.find(key);
        if (it == parameters.end())
            return defaultVal;

        return ParseBoolean(key, it->second);
    }

    /**
     * @brief Retrieves an integer value.
     * @param key The parameter name.
     * @param defaultVal The value to return if the key is missing.
     */
    int GetInt(const std::string &key, int defaultVal) const
    {
        reject_duplicate(key);
        if (parameters.find(key) != parameters.end())
        {
            return ParseInteger(key, parameters.at(key));
        }
        return defaultVal;
    }

    /**
     * @brief Retrieves a double-precision floating point value.
     * @param key The parameter name.
     * @param defaultVal The value to return if the key is missing.
     */
    double GetDouble(const std::string &key, double defaultVal) const
    {
        reject_duplicate(key);
        if (parameters.find(key) != parameters.end())
        {
            return ParseNumber(key, parameters.at(key));
        }
        return defaultVal;
    }

    /**
     * @brief Retrieves a string value.
     * @param key The parameter name.
     * @param defaultVal The value to return if the key is missing.
     */
    std::string GetString(const std::string &key, const std::string &defaultVal) const
    {
        reject_duplicate(key);
        if (parameters.find(key) != parameters.end())
        {
            return parameters.at(key);
        }
        return defaultVal;
    }

    /**
     * @brief Returns unambiguous, trimmed key-value pairs after syntax validation.
     * Useful for iterating over custom parameters that are not hard-coded.
     */
    const std::map<std::string, std::string> &GetAllParams() const
    {
        ThrowIfInvalid();
        return parameters;
    }

    /**
     * @brief Checks if a parameter key exists in the configuration file.
     * @param key The parameter name.
     * @return true if the key exists, false otherwise.
     */
    bool HasKey(const std::string &key) const
    {
        return occurrences.find(key) != occurrences.end();
    }
};
