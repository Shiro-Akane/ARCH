/**
 * @file ConfigParser.h
 * @brief A lightweight, text-based configuration file parser.
 * Supports simple "key = value" syntax.
 * Handles inline comments (starting with '#') and whitespace trimming.
 * Provides type-safe accessors (Bool, Int, Double, String) with default fallbacks.
 */

#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include "../physics/constant/PhysicalConstants.h"

class ConfigValueError : public std::invalid_argument {
public:
    std::string key, code;
    ConfigValueError(std::string parameter, std::string error_code, const std::string& detail)
        : std::invalid_argument("Config parameter '" + parameter + "': " + detail),
          key(std::move(parameter)), code(std::move(error_code)) {}
};

class ConfigParser
{
private:
    /// Storage for parsed key-value pairs
    std::map<std::string, std::string> parameters;

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
     * @return true if file opened and parsed successfully, false otherwise.
     */
    bool Load(const std::string &filename)
    {
        std::ifstream file(filename);
        if (!file.is_open())
        {
            std::cerr << "[Warning] Config file " << filename << " not found! Using defaults." << std::endl;
            return false;
        }

        return Load(file, filename);
    }

    // The application API uses the same parser for an unsaved working copy.
    bool Load(std::istream &input, const std::string &source = "<memory>")
    {
        parameters.clear();

        std::string line;
        while (std::getline(input, line))
        {
            // 1. Strip comments (content after '#')
            size_t commentPos = line.find('#');
            if (commentPos != std::string::npos)
            {
                line = line.substr(0, commentPos);
            }

            // 2. Trim whitespace
            line = trim(line);
            if (line.empty())
                continue;

            // 3. Parse "Key = Value"
            size_t delimPos = line.find('=');
            if (delimPos != std::string::npos)
            {
                std::string key = trim(line.substr(0, delimPos));
                std::string value = trim(line.substr(delimPos + 1));
                parameters[key] = value;
            }
        }
        std::cout << "[Info] Loaded " << parameters.size() << " parameters from " << source << std::endl;
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
        const auto it = parameters.find(key);
        if (it == parameters.end())
            return defaultVal;

        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (value == "true")
            return true;
        if (value == "false")
            return false;

        throw ConfigValueError(key, "INVALID_BOOLEAN",
            "Expected true or false (case-insensitive).");
    }

    /**
     * @brief Retrieves an integer value.
     * @param key The parameter name.
     * @param defaultVal The value to return if the key is missing.
     */
    int GetInt(const std::string &key, int defaultVal) const
    {
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
        if (parameters.find(key) != parameters.end())
        {
            return parameters.at(key);
        }
        return defaultVal;
    }

    /**
     * @brief Returns the raw map of all parsed key-value pairs.
     * Useful for iterating over custom parameters that are not hard-coded.
     */
    const std::map<std::string, std::string> &GetAllParams() const
    {
        return parameters;
    }

    /**
     * @brief Checks if a parameter key exists in the configuration file.
     * @param key The parameter name.
     * @return true if the key exists, false otherwise.
     */
    bool HasKey(const std::string &key) const
    {
        return parameters.find(key) != parameters.end();
    }
};
