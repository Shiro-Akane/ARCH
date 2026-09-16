/**
 * @file RegridTransferMath.h
 * @brief One Host/device authority for conservative regrid cell-family math.
 *
 * These are full-cell migration operations, not coarse/fine ghost filling.
 * Backends supply samples, physical volumes, and scratch storage; the limited
 * reconstruction, conservation correction, fluid admissibility, and simplex
 * projection are identical on both backends. Morton/topology is not included.
 */
#pragma once

#include "ConservativeRestriction.h"
#include "core/ArchPortability.h"
#include "data/FluidState.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace amr::regrid_math {

inline constexpr int maximum_children = 1 << 3;
// rhoX[S][maximum_children], deviation[S][maximum_children], parent_X[S].
inline constexpr int prolongation_workspace_per_species = 2 * maximum_children + 1;
inline constexpr int restriction_workspace_per_species = 1;

enum class Status : int {
    Ok = 0,
    InvalidGeometry,
    ProlongationEnuc,
    ParentFluid,
    FineFluid,
    ParentFractions,
    ParentNormalization,
    ParentClosure,
    CompositionProjection,
    CompositionClosure,
    CoarseFluid,
    RestrictionEnuc,
    SpeciesIntegral,
    SpeciesClosure,
};

inline const char* status_message(Status status)
{
    switch (status) {
    case Status::Ok: return "AMR migration completed.";
    case Status::InvalidGeometry: return "AMR migration requires valid cell-family geometry and workspace.";
    case Status::ProlongationEnuc: return "AMR prolongation requires finite ENUC state.";
    case Status::ParentFluid: return "AMR prolongation requires an admissible parent fluid state.";
    case Status::FineFluid: return "AMR prolongation produced an inadmissible fine-cell fluid state.";
    case Status::ParentFractions: return "AMR prolongation requires finite, non-negative parent mass fractions.";
    case Status::ParentNormalization: return "AMR prolongation requires parent mass fractions normalized to one.";
    case Status::ParentClosure: return "AMR prolongation could not close the parent composition simplex.";
    case Status::CompositionProjection: return "AMR composition projection produced an invalid mass fraction.";
    case Status::CompositionClosure: return "AMR composition projection produced a negative mass fraction.";
    case Status::CoarseFluid: return "AMR restriction produced an inadmissible coarse-cell fluid state.";
    case Status::RestrictionEnuc: return "AMR restriction produced invalid ENUC state.";
    case Status::SpeciesIntegral: return "AMR restriction produced an invalid species integral.";
    case Status::SpeciesClosure: return "AMR restriction requires species mass to close to density.";
    }
    return "AMR migration returned an unknown mathematical status.";
}

ARCH_HOST_DEVICE inline double minimum(double a, double b) { return b < a ? b : a; }
ARCH_HOST_DEVICE inline double maximum(double a, double b) { return a < b ? b : a; }
ARCH_HOST_DEVICE inline double unit_clamp(double value)
{
    return minimum(maximum(value, 0.0), 1.0);
}
ARCH_HOST_DEVICE inline double minmod(double a, double b)
{
    if (a * b > 0.0) return a > 0.0 ? minimum(a, b) : maximum(a, b);
    return 0.0;
}

ARCH_HOST_DEVICE inline bool is_admissible_conserved_state(
    const FluidVector& state, double density_floor, double min_eint)
{
    if (!std::isfinite(state.rho) || !std::isfinite(state.mom_u)
        || !std::isfinite(state.mom_v) || !std::isfinite(state.mom_w)
        || !std::isfinite(state.eng) || state.rho < density_floor)
        return false;
    const double kinetic = 0.5 * (state.mom_u * state.mom_u
        + state.mom_v * state.mom_v + state.mom_w * state.mom_w) / state.rho;
    return std::isfinite(kinetic) && state.eng - kinetic >= state.rho * min_eint;
}

ARCH_HOST_DEVICE inline double composition_simplex_tolerance(int count)
{
    return 64.0 * std::numeric_limits<double>::epsilon()
        * static_cast<double>(count > 1 ? count : 1);
}

ARCH_HOST_DEVICE inline FluidVector blend_conserved_state(
    const FluidVector& average, const FluidVector& candidate, double theta)
{
    if (theta <= 0.0) return average;
    if (theta >= 1.0) return candidate;
    return {average.rho + theta * (candidate.rho - average.rho),
            average.mom_u + theta * (candidate.mom_u - average.mom_u),
            average.mom_v + theta * (candidate.mom_v - average.mom_v),
            average.mom_w + theta * (candidate.mom_w - average.mom_w),
            average.eng + theta * (candidate.eng - average.eng)};
}

/** Non-owning backend-neutral SoA samples. Species are stored as X, not rhoX. */
struct ConstStateView {
    const double* fields[6]{};
    const double* fractions = nullptr;
    std::size_t species_stride = 0;

    ARCH_HOST_DEVICE double fraction(int species, int cell) const
    {
        return fractions[static_cast<std::size_t>(species) * species_stride + cell];
    }
    ARCH_HOST_DEVICE double conserved_sample(int field, int cell) const
    {
        return field < 6 ? fields[field][cell]
            : fields[0][cell] * fraction(field - 6, cell);
    }
    ARCH_HOST_DEVICE FluidVector fluid(int cell) const
    {
        return {fields[0][cell], fields[1][cell], fields[2][cell],
                fields[3][cell], fields[4][cell]};
    }
};

struct ProlongationGeometry {
    int dimension = 0;
    int center = 0;
    int neighbours[6]{};
    double coarse_volume = 0.0;
    double fine_volumes[maximum_children]{};
};

struct ProlongationResult {
    FluidVector fluid[maximum_children]{};
    double enuc[maximum_children]{};
    // The result uses rhoX[species * 8 + child]. Caller supplies 17*S doubles:
    // 8*S rhoX, 8*S deviations, and S normalized parent fractions.
    double* rhoX = nullptr;
};

ARCH_HOST_DEVICE inline double& component(FluidVector& value, int field)
{
    if (field == 0) return value.rho;
    if (field == 1) return value.mom_u;
    if (field == 2) return value.mom_v;
    if (field == 3) return value.mom_w;
    return value.eng;
}

ARCH_HOST_DEVICE inline double reconstruct(
    ConstStateView source, const ProlongationGeometry& geometry,
    int field, int child)
{
    const double center = source.conserved_sample(field, geometry.center);
    const double sx = minmod(center - source.conserved_sample(field, geometry.neighbours[0]),
                            source.conserved_sample(field, geometry.neighbours[1]) - center);
    const double sy = geometry.dimension >= 2
        ? minmod(center - source.conserved_sample(field, geometry.neighbours[2]),
                 source.conserved_sample(field, geometry.neighbours[3]) - center) : 0.0;
    const double sz = geometry.dimension == 3
        ? minmod(center - source.conserved_sample(field, geometry.neighbours[4]),
                 source.conserved_sample(field, geometry.neighbours[5]) - center) : 0.0;
    // Accumulate limited slopes in x, y, z order on both backends. Inactive
    // axes contribute zero; each child lies at +/-1/4 of the parent width.
    return center + ((child & 1) ? 0.25 : -0.25) * sx
        + ((child & 2) ? 0.25 : -0.25) * sy
        + ((child & 4) ? 0.25 : -0.25) * sz;
}

ARCH_HOST_DEVICE inline Status prolong_family(
    ConstStateView source, const ProlongationGeometry& geometry,
    int species, double density_floor, double min_eint,
    double* workspace, ProlongationResult& result)
{
    if (geometry.dimension < 1 || geometry.dimension > 3 || species < 0
        || (species > 0 && workspace == nullptr)
        || !std::isfinite(geometry.coarse_volume) || geometry.coarse_volume <= 0.0)
        return Status::InvalidGeometry;
    const int cells = 1 << geometry.dimension;
    result.rhoX = workspace;
    double* deviation = species > 0 ? workspace
        + static_cast<std::size_t>(species) * maximum_children : nullptr;
    double* parent_X = species > 0 ? workspace
        + static_cast<std::size_t>(species) * (2 * maximum_children) : nullptr;
    for (int cell = 0; cell < cells; ++cell) {
        if (!std::isfinite(geometry.fine_volumes[cell]) || geometry.fine_volumes[cell] <= 0.0)
            return Status::InvalidGeometry;
        for (int field = 0; field < 5; ++field)
            component(result.fluid[cell], field) = reconstruct(source, geometry, field, cell);
        result.enuc[cell] = reconstruct(source, geometry, 5, cell);
        for (int sp = 0; sp < species; ++sp)
            result.rhoX[static_cast<std::size_t>(sp) * maximum_children + cell] = reconstruct(source, geometry, 6 + sp, cell);
    }
    for (int field = 0; field < 5; ++field) {
        double integral = 0.0;
        for (int cell = 0; cell < cells; ++cell)
            integral += component(result.fluid[cell], field) * geometry.fine_volumes[cell];
        const double shift = source.fields[field][geometry.center] - integral / geometry.coarse_volume;
        for (int cell = 0; cell < cells; ++cell)
            component(result.fluid[cell], field) += shift;
    }
    double enuc_integral = 0.0;
    for (int cell = 0; cell < cells; ++cell)
        enuc_integral += result.enuc[cell] * geometry.fine_volumes[cell];
    const double enuc_shift = source.fields[5][geometry.center] - enuc_integral / geometry.coarse_volume;
    for (int cell = 0; cell < cells; ++cell) {
        result.enuc[cell] += enuc_shift;
        if (!std::isfinite(result.enuc[cell])) return Status::ProlongationEnuc;
    }
    const FluidVector parent = source.fluid(geometry.center);
    if (!is_admissible_conserved_state(parent, density_floor, min_eint)) return Status::ParentFluid;
    double fluid_theta = 1.0;
    for (int cell = 0; cell < cells; ++cell)
        if (result.fluid[cell].rho < density_floor)
            fluid_theta = minimum(fluid_theta, (parent.rho - density_floor)
                / (parent.rho - result.fluid[cell].rho));
    fluid_theta = unit_clamp(fluid_theta);
    for (int cell = 0; cell < cells; ++cell) {
        const FluidVector candidate = result.fluid[cell];
        if (is_admissible_conserved_state(blend_conserved_state(parent, candidate, fluid_theta),
                                         density_floor, min_eint)) continue;
        double lower = 0.0;
        double upper = fluid_theta;
        for (int iteration = 0; iteration < 64; ++iteration) {
            const double midpoint = 0.5 * (lower + upper);
            if (is_admissible_conserved_state(blend_conserved_state(parent, candidate, midpoint),
                                             density_floor, min_eint)) lower = midpoint;
            else upper = midpoint;
        }
        fluid_theta = lower;
    }
    if (fluid_theta < 1.0) fluid_theta *= 1.0 - 64.0 * std::numeric_limits<double>::epsilon();
    for (int cell = 0; cell < cells; ++cell) {
        result.fluid[cell] = blend_conserved_state(parent, result.fluid[cell], fluid_theta);
        if (!is_admissible_conserved_state(result.fluid[cell], density_floor, min_eint))
            return Status::FineFluid;
    }
    int closure_species = -1;
    double parent_sum = 0.0;
    const double tolerance = composition_simplex_tolerance(species);
    for (int sp = 0; sp < species; ++sp) {
        const double value = source.fraction(sp, geometry.center);
        if (!std::isfinite(value) || value < -tolerance) return Status::ParentFractions;
        parent_X[sp] = maximum(0.0, value);
        parent_sum += parent_X[sp];
        if (closure_species < 0 || parent_X[sp] > parent_X[closure_species]) closure_species = sp;
    }
    if (species > 0) {
        if (!std::isfinite(parent_sum) || std::abs(parent_sum - 1.0) > tolerance)
            return Status::ParentNormalization;
        parent_X[closure_species] += 1.0 - parent_sum;
        if (parent_X[closure_species] < 0.0) return Status::ParentClosure;
    }
    for (int sp = 0; sp < species; ++sp) {
        double integral = 0.0;
        for (int cell = 0; cell < cells; ++cell)
            integral += result.rhoX[static_cast<std::size_t>(sp) * maximum_children + cell] * geometry.fine_volumes[cell];
        const double target = parent.rho * parent_X[sp];
        const double shift = target - integral / geometry.coarse_volume;
        for (int cell = 0; cell < cells; ++cell) result.rhoX[static_cast<std::size_t>(sp) * maximum_children + cell] += shift;
    }
    if (species > 0) {
        double excess[maximum_children]{};
        for (int cell = 0; cell < cells; ++cell) {
            double sum = 0.0;
            for (int sp = 0; sp < species; ++sp) sum += result.rhoX[static_cast<std::size_t>(sp) * maximum_children + cell];
            excess[cell] = sum - result.fluid[cell].rho;
        }
        double theta = 1.0;
        for (int sp = 0; sp < species; ++sp) {
            for (int cell = 0; cell < cells; ++cell) {
                const double baseline = parent_X[sp] * result.fluid[cell].rho;
                const std::size_t index = static_cast<std::size_t>(sp) * maximum_children + cell;
                deviation[index] = result.rhoX[index] - baseline - parent_X[sp] * excess[cell];
                if (deviation[index] < 0.0) theta = minimum(theta, baseline / -deviation[index]);
            }
        }
        theta = unit_clamp(theta);
        if (theta < 1.0) theta *= 1.0 - 32.0 * std::numeric_limits<double>::epsilon();
        for (int cell = 0; cell < cells; ++cell) {
            double sum = 0.0;
            for (int sp = 0; sp < species; ++sp) {
                const double baseline = parent_X[sp] * result.fluid[cell].rho;
                const std::size_t index = static_cast<std::size_t>(sp) * maximum_children + cell;
                result.rhoX[index] = baseline + theta * deviation[index];
                if (!std::isfinite(result.rhoX[index]) || result.rhoX[index] < 0.0)
                    return Status::CompositionProjection;
                sum += result.rhoX[index];
            }
            result.rhoX[static_cast<std::size_t>(closure_species) * maximum_children + cell] += result.fluid[cell].rho - sum;
            if (!std::isfinite(result.rhoX[static_cast<std::size_t>(closure_species) * maximum_children + cell])
                || result.rhoX[static_cast<std::size_t>(closure_species) * maximum_children + cell] < 0.0)
                return Status::CompositionClosure;
        }
    }
    return Status::Ok;
}

struct RestrictionGeometry {
    int count = 0;
    int source_cells[maximum_children]{};
    double volumes[maximum_children]{};
    double coarse_volume = 0.0;
};

struct RestrictionResult {
    FluidVector fluid{};
    double enuc = 0.0;
    double* fractions = nullptr;
};

ARCH_HOST_DEVICE inline Status restrict_family(
    ConstStateView source, const RestrictionGeometry& geometry, int species,
    double density_floor, double min_eint, double* workspace,
    RestrictionResult& result)
{
    if ((geometry.count != 2 && geometry.count != 4 && geometry.count != 8)
        || species < 0 || (species > 0 && workspace == nullptr)
        || !std::isfinite(geometry.coarse_volume) || geometry.coarse_volume <= 0.0)
        return Status::InvalidGeometry;
    FluidVector integral{};
    double enuc_integral = 0.0;
    for (int sp = 0; sp < species; ++sp) workspace[sp] = 0.0;
    for (int cell = 0; cell < geometry.count; ++cell) {
        const int index = geometry.source_cells[cell];
        const double volume = geometry.volumes[cell];
        if (!std::isfinite(volume) || volume <= 0.0) return Status::InvalidGeometry;
        integral = integral + source.fluid(index) * volume;
        enuc_integral += source.fields[5][index] * volume;
        for (int sp = 0; sp < species; ++sp)
            workspace[sp] += restriction_math::weighted_species_density(
                source.fields[0][index], source.fraction(sp, index), volume);
    }
    result.fluid = integral * (1.0 / geometry.coarse_volume);
    if (!is_admissible_conserved_state(result.fluid, density_floor, min_eint)) return Status::CoarseFluid;
    result.enuc = enuc_integral / geometry.coarse_volume;
    if (!std::isfinite(result.enuc)) return Status::RestrictionEnuc;
    double total = 0.0;
    for (int sp = 0; sp < species; ++sp) {
        if (!std::isfinite(workspace[sp]) || workspace[sp] < 0.0) return Status::SpeciesIntegral;
        total += workspace[sp];
        workspace[sp] = restriction_math::restricted_mass_fraction(workspace[sp], integral.rho);
    }
    if (species > 0) {
        const double scale = maximum(std::abs(integral.rho), std::numeric_limits<double>::min());
        if (!std::isfinite(total) || std::abs(total - integral.rho)
            > composition_simplex_tolerance(species) * scale) return Status::SpeciesClosure;
    }
    result.fractions = workspace;
    return Status::Ok;
}

} // namespace amr::regrid_math
