/** @file ScalarFieldView.h
 * @brief Borrowed scalar storage independent of fluid/species packing.
 * Device addresses are opaque on the host. The owner must outlive every
 * submitted consumer; a view never allocates, transfers or publishes data.
 * Workflow:
 * 1. Borrow the field storage and its native grid layout.
 * 2. Describe borrowed host/device scalar storage with layout and generation identity.
 * 3. Carry memory location and generation identity to the consumer.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace arch::grid {
enum class FieldMemory : std::uint8_t { Host, Device };
enum class FieldCentering : std::uint8_t { Cell, FaceX1, FaceX2, FaceX3 };

struct ScalarFieldLayout {
    int dimension = 0;
    std::array<std::size_t, 3> extent{}; // Includes ghosts/padding.
    std::array<std::size_t, 3> stride{}; // In scalar elements.
    std::array<std::size_t, 3> active_begin{}, active_end{}; // Half-open box.
    FieldCentering centering = FieldCentering::Cell;
    friend bool operator==(const ScalarFieldLayout&, const ScalarFieldLayout&) = default;
};

template<class Scalar>
struct BasicScalarFieldView {
    Scalar* data = nullptr;
    std::size_t size = 0;
    ScalarFieldLayout layout{};
    FieldMemory memory = FieldMemory::Host;
    std::uint64_t storage_generation = 0;
};
using ScalarFieldView = BasicScalarFieldView<double>;
using ConstScalarFieldView = BasicScalarFieldView<const double>;
static_assert(std::is_trivially_copyable_v<ScalarFieldView>);
static_assert(std::is_trivially_copyable_v<ConstScalarFieldView>);
} // namespace arch::grid
