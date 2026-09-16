/**
 * @file ProblemRegistry.h
 * @brief Singleton factory for registering and creating simulation problems.
 * Implements the Factory Design Pattern to decouple problem implementation
 * from the main execution loop.
 */

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "../interface/ProblemGenerator.h"

/**
 * @brief Callback signature to instantiate a specific problem generator.
 * Returns a unique_ptr to ensure ownership transfer to the caller.
 */
using ProblemCreator = std::function<std::unique_ptr<ProblemGenerator>()>;

class ProblemRegistry
{
public:
    /**
     * @brief Access the global singleton instance (Meyers' Singleton).
     * Guaranteed to be thread-safe in C++11 and later.
     */
    static ProblemRegistry &Get()
    {
        static ProblemRegistry instance;
        return instance;
    }

    /**
     * @brief Registers a new problem type with a unique string key.
     * Typically called by static initializers in problem-specific .cpp files.
     * @param name The unique identifier for the problem (e.g., "Sod", "Sedov").
     * @param creator The lambda/function to create the object.
     */
    void Register(const std::string &name, ProblemCreator creator)
    {
        creators_[name] = creator;
    }

    /**
     * @brief Instantiates a problem generator based on the provided name.
     * @param name The lookup key (usually from the input parameter file).
     * @return std::unique_ptr<ProblemGenerator> or nullptr if not found.
     */
    std::unique_ptr<ProblemGenerator> Create(const std::string &name)
    {
        // Use C++20 contains() if available, otherwise use find()
        if (creators_.find(name) != creators_.end())
        {
            return creators_[name]();
        }
        return nullptr; // Caller must check for validity!
    }

private:
    /// Map connecting string keys (from inputs) to factory functions.
    std::map<std::string, ProblemCreator> creators_;

    // Private constructor/destructor for Singleton pattern
    ProblemRegistry() = default;
    ~ProblemRegistry() = default;

    // Delete copy/move semantics to enforce Singleton uniqueness
    ProblemRegistry(const ProblemRegistry &) = delete;
    ProblemRegistry &operator=(const ProblemRegistry &) = delete;
};
