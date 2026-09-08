/**
 * @file regrid_migration_fixture.h
 * @brief Construct shared initial states for regrid migration tests.
 *
 * Build geometric parent/child blocks and seed nonuniform conserved fields,
 * including signed burn energy and trace-species composition.
 */
#pragma once

#include "amr/Block.h"

#include <array>
#include <cmath>
#include <string>

namespace amr::test {

inline Block regrid_block(int dimension, int level, int child, int species,
                         const std::string& geometry = "cartesian")
{
    Grid root(MAX_NG, 1.0, 2.0, 0.4, 1.0, 0.2, 0.8,
              1, dimension >= 2 ? 1 : 0, dimension == 3 ? 1 : 0);
    root.dim = dimension;
    root.geometry = geometry;
    root.InitializeTopology();
    Block block{};
    block.level = level;
    block.logical_x1 = level > 0 ? child & 1 : 0;
    block.logical_x2 = level > 0 && dimension >= 2 ? (child >> 1) & 1 : 0;
    block.logical_x3 = level > 0 && dimension == 3 ? (child >> 2) & 1 : 0;
    block.InitGeometry(root, root.dx1, root.dx2, root.dx3);
    block.fluid_state.Preallocate(block.grid.GetTotalSize());
    block.fluid_state.InitSpecies(species);
    return block;
}

inline void seed_regrid_parent(Block& block)
{
    const auto& grid = block.grid;
    auto& state = block.fluid_state;
    const int species = state.GetNumSpecies();
    for (int k = 0; k < grid.GetTotalZ(); ++k) {
        for (int j = 0; j < grid.GetTotalY(); ++j) {
            for (int i = 0; i < grid.GetTotalX(); ++i) {
                const int cell = grid.GetIndex(i, j, k);
                const double x = (i - grid.Is()) / 16.0;
                const double y = (j - grid.Js()) / 16.0;
                const double z = (k - grid.Ks()) / 16.0;
                state.rho[cell] = 1.0 + 0.1 * x + 0.05 * y + 0.025 * z;
                state.mom_u[cell] = 0.1 + 0.02 * x;
                state.mom_v[cell] = -0.2 + 0.03 * y;
                state.mom_w[cell] = 0.05 - 0.01 * z;
                state.eng[cell] = 2.5 + 0.05 * x + 0.025 * y;
                state.enuc_rate[cell] = -1.0 + 0.03 * x - 0.02 * y + 0.04 * z;
                if (species == 4) {
                    state.X(0, cell) = 0.3 + 0.01 * x;
                    state.X(1, cell) = 0.6 - 0.02 * x;
                    state.X(2, cell) = 0.1 + 0.01 * x;
                    state.X(3, cell) = 0.0;
                } else if (species > 0) {
                    const double weight = 0.5 * (species - 1) * species;
                    for (int sp = 0; sp + 1 < species; ++sp)
                        state.X(sp, cell) = (1.0 - 1.0e-13) * (sp + 1.0) / weight;
                    state.X(species - 1, cell) = species == 1 ? 1.0 : 1.0e-13;
                }
            }
        }
    }
}

inline std::array<const double*, 6> regrid_fields(const FluidState& state)
{
    return {state.rho.data(), state.mom_u.data(), state.mom_v.data(),
            state.mom_w.data(), state.eng.data(), state.enuc_rate.data()};
}

} // namespace amr::test
