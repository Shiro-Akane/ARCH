/**
 * @file ExternalGravity.h
 * @brief Evaluates configured external gravity fields and their source coupling.
 *
 * Workflow:
 * 1. Construct the selected gravity policy from runtime configuration.
 * 2. Evaluate accelerations or potentials on the current active AMR geometry.
 * 3. Apply gravity only through the common source-term interface used by every integrator.
 */

/**
 * ExternalGravity.h
 * @brief Implements an external gravity model for the simulation.
 * This model applies a constant gravitational field throughout the domain.
 */

#pragma once
#include "../../data/FluidState.h"
#include "../../grid/Grid.h"
#include "IGravityPolicy.h"

namespace Physical {
namespace Gravity {

struct ExternalGravity : public IGravityPolicy
{
    double g_x, g_y, g_z;

    ExternalGravity(double gx, double gy, double gz) : g_x(gx), g_y(gy), g_z(gz) {}

    virtual void update_field(const FluidState &state, const Grid &grid, void* execution_stream = nullptr) const override {}

    virtual void add_sources_on_patch(std::vector<FluidVector>& dU, const FluidState& state,
                                      const Grid& grid, double dt, void* execution_stream = nullptr) const override
    {
        const int ks = grid.Ks(), ke = grid.Ke();
        const int js = grid.Js(), je = grid.Je();
        const int nk = ke - ks, nj = je - js;

#pragma omp parallel for schedule(static)
        for (int kj = 0; kj < nk * nj; ++kj)
        {
            int k = ks + kj / nj;
            int j = js + kj % nj;
            for (int i = grid.Is(); i < grid.Ie(); ++i)
            {
                int idx = grid.GetIndex(i, j, k);
                double rho = state.rho[idx];

                if (rho < 1e-12)
                    continue;

                double vx = state.mom_u[idx] / rho;
                double vy = state.mom_v[idx] / rho;
                double vz = state.mom_w[idx] / rho;

                dU[idx].mom_u += dt * rho * g_x;
                dU[idx].mom_v += dt * rho * g_y;
                dU[idx].mom_w += dt * rho * g_z;
                dU[idx].eng += dt * rho * (vx * g_x + vy * g_y + vz * g_z);
            }
        }
    }
};

}
}