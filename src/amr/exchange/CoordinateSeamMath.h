/**
 * @file CoordinateSeamMath.h
 * @brief Reconstruct one coordinate-singularity ghost from a physical AMR donor.
 *
 * Workflow:
 * 1. Receive a topology-owned donor stencil and borrowed structure-of-arrays views.
 * 2. Interpolate conserved fields and mass fractions at the mapped physical point.
 * 3. Rotate native momentum components and use the active donor center if the
 *    ghost-only interpolation leaves the admissible state domain.
 *
 * Host and CUDA call this same arithmetic. The topology lookup and device
 * memory ownership remain with their respective AMR executors.
 */
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "amr/transfer/RegridTransferMath.h"
#include "numerics/state/StateAdmissibility.h"

namespace amr {

/** Immutable cell indices, interpolation weights and native-basis rotation. */
struct CoordinateSeamTransfer {
    int source_id = -1;
    int destination_id = -1;
    int source_center = -1;
    int destination_cell = -1;
    std::array<int, 3> source_neighbor{};
    std::array<double, 3> neighbor_weight{};
    std::array<std::int8_t, 3> momentum_sign{1, 1, 1};
};

/** Borrowed SoA fields; Scalar is const double for a donor and double for a ghost. */
template<class Scalar> struct CoordinateSeamFields {
    Scalar *rho, *mom_u, *mom_v, *mom_w, *eng, *enuc_rate, *fractions;
    int total_size;
    int species_count;

    /** Read one donor cell without taking ownership of its field storage. */
    ARCH_INLINE FluidVector load(int cell) const {
        return {rho[cell], mom_u[cell], mom_v[cell], mom_w[cell], eng[cell]};
    }
    /** Read one species fraction from the shared SoA layout. */
    ARCH_INLINE double species(int species_index, int cell) const {
        return fractions[species_index * total_size + cell];
    }
};

/** Return false only when the already published active donor itself is invalid. */
ARCH_INLINE bool apply_coordinate_seam_transfer(
    const CoordinateSeamTransfer& transfer,
    CoordinateSeamFields<const double> source,
    CoordinateSeamFields<double> destination)
{
    const auto sample = [&](const double* field) {
        double value = field[transfer.source_center];
        for (int axis = 0; axis < 3; ++axis)
            value += transfer.neighbor_weight[axis]
                * (field[transfer.source_neighbor[axis]]
                   - field[transfer.source_center]);
        return value;
    };
    const int cell = transfer.destination_cell;
    // U_g = U_c + sum_a [(x_g-x_c)/(x_n-x_c)] (U_n-U_c).
    // Momentum signs are the orthonormal-basis transformation at r=0/poles.
    const FluidVector candidate{
        sample(source.rho),
        transfer.momentum_sign[0] * sample(source.mom_u),
        transfer.momentum_sign[1] * sample(source.mom_v),
        transfer.momentum_sign[2] * sample(source.mom_w),
        sample(source.eng)};
    const double enuc = sample(source.enuc_rate);
    bool admissible = arch::state::recover(candidate).status
        == arch::state::Status::valid && std::isfinite(enuc);
    double species_sum = 0.;
    const double species_tolerance = regrid_math::composition_simplex_tolerance(
        source.species_count);
    for (int species = 0; species < source.species_count; ++species) {
        double value = source.species(species, transfer.source_center);
        for (int axis = 0; axis < 3; ++axis)
            value += transfer.neighbor_weight[axis]
                * (source.species(species, transfer.source_neighbor[axis])
                   - source.species(species, transfer.source_center));
        destination.fractions[species * destination.total_size + cell] = value;
        species_sum += value;
        admissible &= std::isfinite(value) && value >= 0. && value <= 1.;
    }
    if (source.species_count > 0)
        admissible &= std::abs(species_sum - 1.) <= species_tolerance;
    if (admissible) {
        destination.rho[cell] = candidate.rho;
        destination.mom_u[cell] = candidate.mom_u;
        destination.mom_v[cell] = candidate.mom_v;
        destination.mom_w[cell] = candidate.mom_w;
        destination.eng[cell] = candidate.eng;
        destination.enuc_rate[cell] = enuc;
        return true;
    }
    // A steep one-sided AMR slope may leave the admissible domain. The
    // fallback changes this ghost only, never an active conserved value.
    FluidVector center = source.load(transfer.source_center);
    center.mom_u *= transfer.momentum_sign[0];
    center.mom_v *= transfer.momentum_sign[1];
    center.mom_w *= transfer.momentum_sign[2];
    if (arch::state::recover(center).status != arch::state::Status::valid)
        return false;
    destination.rho[cell] = center.rho;
    destination.mom_u[cell] = center.mom_u;
    destination.mom_v[cell] = center.mom_v;
    destination.mom_w[cell] = center.mom_w;
    destination.eng[cell] = center.eng;
    destination.enuc_rate[cell] = source.enuc_rate[transfer.source_center];
    for (int species = 0; species < source.species_count; ++species)
        destination.fractions[species * destination.total_size + cell]
            = source.species(species, transfer.source_center);
    return true;
}

} // namespace amr
