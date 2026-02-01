/**
 * @file ConfigParser.h
 * @brief A lightweight, text-based configuration file parser.
 * * Supports simple "key = value" syntax.
 * * Handles inline comments (starting with '#') and whitespace trimming.
 * * Provides type-safe accessors (Int, Double, String) with default fallbacks.
 */

#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <algorithm>

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
    /**
     * @brief Loads and parses the specified configuration file.
     * * Parsing Rules:
     * 1. Ignores empty lines.
     * 2. Ignores text after '#' (comments).
     * 3. Splits lines by the first '=' character into Key and Value.
     * 4. Trims whitespace around Keys and Values.
     * * @param filename Path to the configuration file.
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

        std::string line;
        while (std::getline(file, line))
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
        std::cout << "[Info] Loaded " << parameters.size() << " parameters from " << filename << std::endl;
        return true;
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
            return std::stoi(parameters.at(key));
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
            return std::stod(parameters.at(key));
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
};