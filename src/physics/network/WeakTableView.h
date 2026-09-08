/**
 * @file WeakTableView.h
 * @brief Non-owning immutable weak-table storage, without interpolation or reaction math.
 *
 * Host generated data and a CUDA allocation may bind the same views. Owners
 * must outlive every evaluator/continuation using their addresses.
 */
#pragma once
#include "core/ArchPortability.h"
#include <cstddef>
#include <limits>

namespace arch::network {
struct WeakTableStorageView {
    const double* data = nullptr;
    std::size_t size = 0;
    ARCH_HOST_DEVICE const double* slice(std::size_t offset, std::size_t count) const {
        return data && offset <= size && count <= size - offset ? data + offset : nullptr;
    }
};
struct WeakAxisView {
    const double* data = nullptr;
    int count = 0;
    ARCH_HOST_DEVICE int lo() const { return 1; }
    ARCH_HOST_DEVICE int hi() const { return count; }
    ARCH_HOST_DEVICE int size() const { return count; }
    ARCH_HOST_DEVICE double operator()(int index) const {
        return data && index >= lo() && index <= hi()
            ? data[index - lo()] : std::numeric_limits<double>::quiet_NaN();
    }
};
struct WeakValuesView {
    const double* data = nullptr;
    int temperatures = 0, densities = 0, components = 0;
    // Preserve SimpleCxx's one-based Fortran layout: T, rho*Ye, component.
    ARCH_HOST_DEVICE double operator()(int temperature, int density, int component) const {
        if (!data || temperature < 1 || temperature > temperatures
            || density < 1 || density > densities || component < 1 || component > components)
            return std::numeric_limits<double>::quiet_NaN();
        const auto offset = static_cast<std::size_t>(temperature - 1)
            + static_cast<std::size_t>(temperatures) * (density - 1
                + static_cast<std::size_t>(densities) * (component - 1));
        return data[offset];
    }
};
} // namespace arch::network
