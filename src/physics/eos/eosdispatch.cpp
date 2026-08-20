/**
 * @file eosdispatch.cpp
 * @brief Helper implementations for the Equation of State dispatcher.
 *
 * Workflow:
 * 1. Provide utility functions (like dimension checking) that rely on heavy libraries (e.g., HighFive).
 * 2. Keep these functions out of the main headers to prevent template bloat during compilation.
 */

#include "eosdispatch.h"

#include <highfive/H5File.hpp>

// Validate table dimensions before constructing an EOS policy.

/**
 * @brief Checks if a given HDF5 EOS table contains 4D dataset attributes (n_A, n_Z).
 * @param path Path to the HDF5 EOS table file.
 * @return true if the table is 4D (e.g., Helmholtz EOS), false otherwise (e.g., 3D).
 */
bool check_eos_is_4d(const std::string& path)
{
    // 1. Inspect HDF5 Metadata
    HighFive::File file(path, HighFive::File::ReadOnly);

    // Check for the existence of composition dimension variables (n_A and n_Z)
    return file.exist("n_A") && file.exist("n_Z");
}
