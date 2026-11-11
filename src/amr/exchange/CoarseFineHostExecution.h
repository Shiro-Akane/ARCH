/**
 * @file CoarseFineHostExecution.h
 * @brief Native Host lowering, gather and scatter for coarse/fine cell recipes.
 *
 * Both local exchange and the source rank of a distributed exchange execute
 * these same operations. Gather owns no remote fields and never writes a
 * destination. GhostExchange publishes only after all gathers are ready.
 */
#pragma once

#include "amr/exchange/CoarseFineCellPlan.h"
#include "amr/storage/Block.h"
#include "amr/transfer/ConservativeRestriction.h"
#include "amr/transfer/LimitedLinearProlongation.h"
#include "numerics/curvilinear/GridMetrics.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace amr::coarse_fine_host {
struct SourceTransfer {
    std::array<int, 8> source_cells{};
    std::array<int, 6> slope_cells{};
    std::array<double, 3> fine_position{};
    std::array<double, 8> source_measures{};
    std::array<double, 8> source_moment_measures{};
    std::uint8_t source_count = 0;
    double source_measure_sum = 0.0;
    double source_moment_measure_sum = 0.0;
    RefinementRule rule = RefinementRule::CoarseGhostInjection;
};
struct Values {
    std::array<double, 6> fields{};
    std::vector<double> mass_fractions;
};
inline int cell_index(const Grid& grid, const LogicalAmrCell& cell) {

    const std::array<std::int64_t, 3> absolute{static_cast<std::int64_t>(grid.Is()) + cell[0],
                                               static_cast<std::int64_t>(grid.Js()) + cell[1],
                                               static_cast<std::int64_t>(grid.Ks()) + cell[2]};
    if (absolute[0] < 0 || absolute[0] >= grid.GetTotalX() || absolute[1] < 0 ||
        absolute[1] >= grid.GetTotalY() || absolute[2] < 0 || absolute[2] >= grid.GetTotalZ())
        throw std::out_of_range("coarse-fine cell is outside Host block layout");
    return grid.GetIndex(static_cast<int>(absolute[0]), static_cast<int>(absolute[1]),
                         static_cast<int>(absolute[2]));
}

inline void validate_state(const Block& block, FluidState Block::* slot, int species) {
    if (!slot || species < 0)
        throw std::invalid_argument("coarse-fine Host state slot or species is invalid");
    const auto& state = block.*slot;
    const auto cells = static_cast<std::size_t>(block.grid.GetTotalSize());
    if (state.GetNumSpecies() != species || state.rho.size() != cells ||
        state.mom_u.size() != cells || state.mom_v.size() != cells || state.mom_w.size() != cells ||
        state.eng.size() != cells || state.enuc_rate.size() != cells ||
        (species != 0 && cells > std::numeric_limits<std::size_t>::max() / species) ||
        state.mass_fractions.size() != cells * species)
        throw std::invalid_argument("coarse-fine Host field extent or species mismatch");
}

inline Values gather(const CoarseFineCellTransfer& recipe, const Block& source_block,
                     FluidState Block::* slot, int dimension, int species_count) {
    const auto lower = [&](const CoarseFineCellTransfer& transfer) {
        SourceTransfer lowered{};
        lowered.source_count = transfer.source_count;
        lowered.fine_position = transfer.fine_position;
        lowered.rule = transfer.rule;
        const bool angular_momentum = source_block.grid.geometry == "cylindrical" &&
                                      dimension == 2 &&
                                      transfer.rule == RefinementRule::FineGhostAverage;
        if (lowered.source_count == 0 || lowered.source_count > lowered.source_cells.size())
            throw std::invalid_argument("coarse-fine Host source count is invalid");
        for (std::size_t cell = 0; cell < lowered.source_count; ++cell) {
            lowered.source_cells[cell] = cell_index(source_block.grid, transfer.source_cells[cell]);
            const LogicalAmrCell& logical = transfer.source_cells[cell];

            // Cartesian fine cells have one common measure, so unit
            // weights preserve the existing arithmetic.  Curvilinear
            // restriction must use physical cell volumes.
            double measure = 1.0;
            if (source_block.grid.geometry != "cartesian") {
                measure = GridMetrics::CellVolume(
                    GridMetrics::make_geometry_view(source_block.grid),
                    source_block.grid.Is() + logical[0], source_block.grid.Js() + logical[1],
                    source_block.grid.Ks() + logical[2]);
            }
            if (!std::isfinite(measure) || measure <= 0.0)
                throw std::invalid_argument("coarse-fine Host source measure is invalid");
            lowered.source_measures[cell] = measure;
            lowered.source_measure_sum += measure;
            if (angular_momentum) {
                const double moment = GridMetrics::AngularMomentumVolumeMoment(
                    GridMetrics::make_geometry_view(source_block.grid),
                    source_block.grid.Is() + logical[0], source_block.grid.Js() + logical[1],
                    source_block.grid.Ks() + logical[2]);
                if (!std::isfinite(moment) || moment <= 0.0)
                    throw std::invalid_argument("coarse-fine Host source moment is invalid");
                lowered.source_moment_measures[cell] = moment;
                lowered.source_moment_measure_sum += moment;
            }
        }
        if (lowered.rule == RefinementRule::CoarseGhostInjection) {
            for (std::size_t cell = 0; cell < lowered.slope_cells.size(); ++cell)
                lowered.slope_cells[cell] =
                    cell_index(source_block.grid, transfer.slope_cells[cell]);
        }
        if (!std::isfinite(lowered.source_measure_sum) || lowered.source_measure_sum <= 0.0)
            throw std::invalid_argument("coarse-fine Host source measure sum is invalid");
        if (angular_momentum && (!std::isfinite(lowered.source_moment_measure_sum) ||
                                 lowered.source_moment_measure_sum <= 0.0))
            throw std::invalid_argument("coarse-fine Host source moment sum is invalid");

        return lowered;
    };
    const SourceTransfer transfer = lower(recipe);
    const FluidState& source = source_block.*slot;
    const prolongation_math::CompositionStencilView stencil{source.rho.data(),
                                                            source.mass_fractions.data(),
                                                            source.rho.size(),
                                                            transfer.source_cells[0],
                                                            transfer.slope_cells.data(),
                                                            dimension,
                                                            species_count};
    const auto transfer_field = [&](const std::vector<double>& field, bool moment_measure) {
        if (transfer.rule == RefinementRule::CoarseGhostInjection)
            return prolongation_math::reconstruct_field(stencil, field.data(),
                                                        transfer.fine_position.data());
        const auto& measures =
            moment_measure ? transfer.source_moment_measures : transfer.source_measures;
        const double measure_sum =
            moment_measure ? transfer.source_moment_measure_sum : transfer.source_measure_sum;
        double integral = 0.0;
        for (std::size_t cell = 0; cell < transfer.source_count; ++cell)
            integral += restriction_math::weighted_conserved_value(
                field[transfer.source_cells[cell]], measures[cell]);
        return restriction_math::restricted_average(integral, measure_sum);
    };
    double density_integral = 0.0;
    for (std::size_t cell = 0; cell < transfer.source_count; ++cell)
        density_integral += restriction_math::weighted_conserved_value(
            source.rho[transfer.source_cells[cell]], transfer.source_measures[cell]);
    Values values{};
    const bool angular_momentum = transfer.rule == RefinementRule::FineGhostAverage &&
                                  transfer.source_moment_measure_sum > 0.0;
    values.fields = {
        transfer_field(source.rho, false),   transfer_field(source.mom_u, false),
        transfer_field(source.mom_v, false), transfer_field(source.mom_w, angular_momentum),
        transfer_field(source.eng, false),   transfer_field(source.enuc_rate, false)};
    values.mass_fractions.resize(static_cast<std::size_t>(species_count));
    if (transfer.rule == RefinementRule::CoarseGhostInjection) {
        const double fine_density = values.fields[0];
        const int closure_species = prolongation_math::composition_closure_species(stencil);
        const auto family =
            prolongation_math::classify_composition_family(stencil, closure_species);
        if (family == prolongation_math::CompositionFamily::InvalidDensity)
            throw std::runtime_error(prolongation_math::invalid_prolongation_density_message());
        for (int species = 0; species < species_count; ++species)
            values.mass_fractions[species] = prolongation_math::reconstruct_mass_fraction(
                stencil, family, fine_density, species, transfer.fine_position.data(),
                closure_species);
    } else {
        for (int species = 0; species < species_count; ++species) {
            double species_density_integral = 0.0;
            for (std::size_t cell = 0; cell < transfer.source_count; ++cell) {
                const int source_cell = transfer.source_cells[cell];
                species_density_integral += restriction_math::weighted_species_density(
                    source.rho[source_cell], source.X(species, source_cell),
                    transfer.source_measures[cell]);
            }
            values.mass_fractions[species] = restriction_math::restricted_mass_fraction(
                species_density_integral, density_integral);
        }
    }

    return values;
}

inline void scatter(FluidState& destination, int cell, const Values& values) noexcept {
    destination.rho[cell] = values.fields[0];
    destination.mom_u[cell] = values.fields[1];
    destination.mom_v[cell] = values.fields[2];
    destination.mom_w[cell] = values.fields[3];
    destination.eng[cell] = values.fields[4];
    destination.enuc_rate[cell] = values.fields[5];
    for (std::size_t species = 0; species < values.mass_fractions.size(); ++species)
        destination.X(static_cast<int>(species), cell) = values.mass_fractions[species];
}
} // namespace amr::coarse_fine_host
