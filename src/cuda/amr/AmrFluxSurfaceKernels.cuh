/**
 * @file AmrFluxSurfaceKernels.cuh
 * @brief CUDA memory execution of shared AMR registration and reflux rules.
 *
 * Compiled host plans supply target cells and geometric weights. Kernels use
 * AmrFluxMath to update borrowed compact surfaces and conservative cell values;
 * the runtime serializes contributors and owns stream completion.
 */

#pragma once

#include "amr/AmrFluxMath.h"
#include "amr/AmrFluxExecutionPlan.h"
#include "cuda/amr/AmrFluxSurfaceTypes.cuh"

namespace arch::cuda::amr_flux_kernel_detail {

__global__ void clear_surface_kernel(
    DeviceAmrFluxSurfaceView surface, int scalar_count)
{
    const int scalar = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (scalar >= scalar_count) return;
    const int field = scalar / surface.cell_count;
    const int cell = scalar - field * surface.cell_count;
    if (field == 0) surface.rho[cell] = 0.0;
    else if (field == 1) surface.mom_u[cell] = 0.0;
    else if (field == 2) surface.mom_v[cell] = 0.0;
    else if (field == 3) surface.mom_w[cell] = 0.0;
    else if (field == 4) surface.eng[cell] = 0.0;
    else surface.set_species_flux(field - 5, cell, 0.0);
}

ARCH_DEVICE inline int face_source_cell(
    const DeviceGridView& grid, int face, int surface_cell)
{
    const int axis = face / 2;
    const int upper = face & 1;
    int i = grid.is;
    int j = grid.js;
    int k = grid.ks;
    if (axis == 0) {
        const int ny = grid.je - grid.js;
        i = upper ? grid.ie : grid.is;
        j += surface_cell % ny;
        k += surface_cell / ny;
    } else if (axis == 1) {
        i += surface_cell % (grid.ie - grid.is);
        j = upper ? grid.je : grid.js;
        k += surface_cell / (grid.ie - grid.is);
    } else {
        i += surface_cell % (grid.ie - grid.is);
        j += surface_cell / (grid.ie - grid.is);
        k = upper ? grid.ke : grid.ks;
    }
    return grid.index(i, j, k);
}

__global__ void capture_surface_kernel(
    const DeviceAmrFluxBlockView* blocks, int source_block,
    int face, int requested_cells)
{
    const int cell = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    if (cell >= requested_cells) return;
    const auto block = blocks[source_block];
    const auto destination = block.initial_flux[face];
    if (cell >= destination.cell_count) return;
    const int source_cell = face_source_cell(block.grid, face, cell);
    destination.rho[cell] = block.stage_flux.rho[source_cell];
    destination.mom_u[cell] = block.stage_flux.mom_u[source_cell];
    destination.mom_v[cell] = block.stage_flux.mom_v[source_cell];
    destination.mom_w[cell] = block.stage_flux.mom_w[source_cell];
    destination.eng[cell] = block.stage_flux.eng[source_cell];
    for (int species = 0; species < destination.species_count; ++species)
        destination.set_species_flux(
            species, cell, block.stage_flux.species(species, source_cell));
}

__global__ void register_route_kernel(
    const DeviceAmrFluxBlockView* blocks, int source_block,
    const amr::AmrFluxRegistrationTarget* targets,
    const amr::AmrFluxRegistrationTerm* terms, int target_count,
    AmrFluxSource source_kind, double stage_weight)
{
    const int target_index = static_cast<int>(
        blockIdx.x * blockDim.x + threadIdx.x);
    if (target_index >= target_count) return;
    const auto target = targets[target_index];
    const auto destination =
        blocks[target.destination_block].registers[target.destination_face];
    const auto stage_source = blocks[source_block].stage_flux;
    const auto surface_source =
        blocks[source_block].initial_flux[target.source_face];

    double rho = 0.0;
    double mom_u = 0.0;
    double mom_v = 0.0;
    double mom_w = 0.0;
    double eng = 0.0;
    for (int offset = 0; offset < target.term_count; ++offset) {
        const auto term = terms[target.first_term + offset];
        const double coefficient = amr::flux_math::registration_coefficient(
            target.rule, term.geometric_weight, stage_weight);
        const int cell = source_kind == AmrFluxSource::StageScratch
            ? term.source_cell : term.source_surface_cell;
        if (source_kind == AmrFluxSource::StageScratch) {
            rho += coefficient * stage_source.rho[cell];
            mom_u += coefficient * stage_source.mom_u[cell];
            mom_v += coefficient * stage_source.mom_v[cell];
            mom_w += coefficient * stage_source.mom_w[cell];
            eng += coefficient * stage_source.eng[cell];
        } else {
            rho += coefficient * surface_source.rho[cell];
            mom_u += coefficient * surface_source.mom_u[cell];
            mom_v += coefficient * surface_source.mom_v[cell];
            mom_w += coefficient * surface_source.mom_w[cell];
            eng += coefficient * surface_source.eng[cell];
        }
    }
    const int cell = target.destination_cell;
    destination.rho[cell] += rho;
    destination.mom_u[cell] += mom_u;
    destination.mom_v[cell] += mom_v;
    destination.mom_w[cell] += mom_w;
    destination.eng[cell] += eng;
    for (int species = 0; species < destination.species_count; ++species) {
        double value = 0.0;
        for (int offset = 0; offset < target.term_count; ++offset) {
            const auto term = terms[target.first_term + offset];
            const double coefficient =
                amr::flux_math::registration_coefficient(
                    target.rule, term.geometric_weight, stage_weight);
            value += coefficient
                * (source_kind == AmrFluxSource::StageScratch
                    ? stage_source.species(species, term.source_cell)
                    : surface_source.species_flux(
                        species, term.source_surface_cell));
        }
        destination.set_species_flux(
            species, cell,
            destination.species_flux(species, cell) + value);
    }
}

__global__ void reflux_kernel(
    const DeviceAmrFluxBlockView* blocks,
    const amr::AmrRefluxTarget* targets,
    const amr::AmrRefluxContribution* contributions,
    int target_count, double dt)
{
    const int target_index = static_cast<int>(
        blockIdx.x * blockDim.x + threadIdx.x);
    if (target_index >= target_count) return;
    const auto target = targets[target_index];
    const auto state = blocks[target.block].state;
    const int state_cell = target.state_cell;
    for (int offset = 0; offset < target.contribution_count; ++offset) {
        const auto contribution =
            contributions[target.first_contribution + offset];
        const auto flux =
            blocks[target.block].registers[contribution.register_face];
        const int flux_cell = contribution.register_cell;
        const double correction =
            dt * contribution.sign * contribution.geometric_weight;
        const double rho_before = state.rho[state_cell];
        const double rho_after = amr::flux_math::reflux_conserved(
            rho_before, correction, flux.rho[flux_cell]);
        state.rho[state_cell] = rho_after;
        state.mom_u[state_cell] = amr::flux_math::reflux_conserved(
            state.mom_u[state_cell], correction, flux.mom_u[flux_cell]);
        state.mom_v[state_cell] = amr::flux_math::reflux_conserved(
            state.mom_v[state_cell], correction, flux.mom_v[flux_cell]);
        state.mom_w[state_cell] = amr::flux_math::reflux_conserved(
            state.mom_w[state_cell], correction, flux.mom_w[flux_cell]);
        state.eng[state_cell] = amr::flux_math::reflux_conserved(
            state.eng[state_cell], correction, flux.eng[flux_cell]);
        for (int species = 0; species < target.species_count; ++species)
            state.set_species(
                species, state_cell,
                amr::flux_math::reflux_mass_fraction(
                    rho_before, state.species(species, state_cell),
                    correction, flux.species_flux(species, flux_cell),
                    rho_after));
    }
}

} // namespace arch::cuda::amr_flux_kernel_detail
