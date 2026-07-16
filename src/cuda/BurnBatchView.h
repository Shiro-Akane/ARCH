#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace arch::cuda {

/**
 * Device-resident, structure-of-arrays input for one burn stage.
 *
 * Every pointer denotes device memory.  Scalar fields are contiguous arrays of
 * cell_count elements.  Species are species-major:
 *
 *     mass_fractions[species * species_stride + cell]
 *
 * species_stride must be at least cell_count.  A launcher may support exact
 * input/output aliasing because each cell is loaded before it is committed,
 * but partial overlap is invalid.
 *
 * This aggregate intentionally contains no FluidState, Grid, EOS virtual
 * object, std::function, HDF5 handle, or host-owned container.  A future Driver
 * adapter should construct this view once and issue one batch launch per burn
 * stage.
 */
struct BurnBatchInputSoA
{
    const double* density = nullptr;
    const double* temperature = nullptr;
    const double* mass_fractions = nullptr;
    const double* dt_target = nullptr;
    const double* heat_capacity_cv = nullptr;
    std::size_t cell_count = 0;
    std::size_t species_stride = 0;
    std::int32_t species_count = 0;
};

/** Device-resident, structure-of-arrays result for one burn stage. */
struct BurnBatchOutputSoA
{
    double* temperature = nullptr;
    double* mass_fractions = nullptr;
    std::uint32_t* status = nullptr;
    double* dt_recommended = nullptr;
    std::size_t cell_count = 0;
    std::size_t species_stride = 0;
    std::int32_t species_count = 0;
};

/**
 * Per-cell status bits.  Zero is success.  Failed cells are transactional: the
 * validation launcher writes the original X/T instead of a partial Newton
 * iterate.  Production policy may retry those cells on CPU only if that choice
 * is explicit and recorded by the caller.
 */
enum BurnCellStatus : std::uint32_t
{
    BurnCellSuccess = 0u,
    BurnCellInvalidInput = 1u << 0,
    BurnCellSingularMatrix = 1u << 1,
    BurnCellNonFinite = 1u << 2,
    BurnCellInadmissible = 1u << 3,
    BurnCellMaxIterations = 1u << 4,
    BurnCellEnergyClosure = 1u << 5,
    BurnCellUnsupportedPhysics = 1u << 6
};

/** Host-side result of enqueueing a batch.  It does not summarize cell status. */
enum class BurnBatchLaunchStatus : std::int32_t
{
    Enqueued = 0,
    EmptyBatch = 1,
    InvalidView = 2,
    InvalidOptions = 3,
    RuntimeError = 4
};

struct BurnBatchLaunchResult
{
    BurnBatchLaunchStatus status = BurnBatchLaunchStatus::InvalidView;
    // Numeric cudaError_t without exposing CUDA headers in this ABI.
    std::int32_t cuda_runtime_error = 0;
    std::size_t enqueued_cells = 0;

    [[nodiscard]] constexpr bool enqueued() const noexcept
    {
        return status == BurnBatchLaunchStatus::Enqueued
            || status == BurnBatchLaunchStatus::EmptyBatch;
    }
};

// Opaque cudaStream_t.  nullptr means CUDA's default stream.  Keeping this
// opaque lets CPU-only translation units include the view definition.
using BurnCudaStream = void*;

static_assert(std::is_standard_layout_v<BurnBatchInputSoA>);
static_assert(std::is_standard_layout_v<BurnBatchOutputSoA>);
static_assert(std::is_trivially_copyable_v<BurnBatchInputSoA>);
static_assert(std::is_trivially_copyable_v<BurnBatchOutputSoA>);

} // namespace arch::cuda
