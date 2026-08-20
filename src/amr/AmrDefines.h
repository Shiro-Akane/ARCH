/**
 * @file AmrDefines.h
 * @brief Global compile-time constants for the AMR (Adaptive Mesh Refinement) module.
 */

/**
 * Workflow:
 * 1. Build or query topology using the single hierarchy and memory-pool ownership model.
 * 2. Synchronize state or face data with the documented 2:1 AMR index convention.
 * 3. Return conservative leaf data to the driver for refluxing, regridding, or timestep work.
 */

#pragma once

#include <cstdint>

// Compile-time constants for Block Geometry
namespace amr {

// Number of ghost cells (guard cells) globally fixed for GPU static allocation.
constexpr int MAX_NG = 4;

// Logical block size (number of active physical cells in each dimension)
constexpr int BLOCK_NX = 16;
constexpr int BLOCK_NY = 16;
constexpr int BLOCK_NZ = 16;

// Total size including ghost cells
constexpr int TOTAL_NX = BLOCK_NX + 2 * MAX_NG; // 16 + 8 = 24

// Padded size for the innermost dimension (X-axis) to ensure 32-thread
// warp-coalesced memory access on CUDA.
constexpr int PAD_NX = 32;

} // namespace amr
