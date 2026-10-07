/**
 * @file RefinementThermodynamics.h
 * @brief Evaluate refinement thermodynamics through the existing EOS bridge without retaining transient state.
 *
 * Workflow:
 * 1. Borrow the actual patch, logical Grid, explicit chart and physical bounds.
 * 2. Interpret Native RZ V/W means through the shared density/inertia closure;
 *    Existing input retains its original padded point-EOS batch arithmetic.
 * 3. Evaluate the unchanged EOS mean thermodynamics and, when requested,
 *    physical center velocities without changing evolved state or padding.
 * 4. Release all temporary buffers/workspaces before the next regrid callback.
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

#include "amr/topology/AmrTree.h"

namespace amr {
// One EOS batch adapter for the simulation driver and the CPU initial mesh preview.
/** Bind one real EOS adapter for mean thermodynamics and physical velocities.
 * Native rotational kinetic mean is J^2/(2 I_* V), while center swirl is
 * u_phi(r_c)=Omega*r_c. The shared closure owns both formulas; this batch
 * merely borrows it and publishes transient observer buffers.
 */
template<class EosPolicy>
void BindRefinementThermodynamics(AmrTree& tree, const EosPolicy& eos) {
    tree.SetJeansEvaluator([&eos](const FluidVector& state, const double* fractions,
        const GridMetrics::GeometryView& geometry, int i, int j) {
        const double pressure=eos.get_pressure(state,fractions);
        const double sound=eos.get_sound_speed(state,pressure,fractions);
        return JeansDiagnostics::evaluate_cell(state.rho,sound*sound,geometry,i,j);
    });
    tree.SetThermodynamicEvaluator([&eos](const FluidState& state,
        const Grid& grid, GridMetrics::GeometrySemantics semantics,
        const arch::state::Bounds& bounds, std::vector<double>* pressure,
        std::vector<double>* temperature, std::vector<double>* gamma1,
        std::array<std::vector<double>,3>* physical_velocity) {
        const int total_size = static_cast<int>(state.rho.size());
        const int n_species = state.GetNumSpecies();
        if (pressure) pressure->assign(total_size, 0.0);
        if (temperature) temperature->assign(total_size, 0.0);
        if (gamma1) gamma1->assign(total_size, std::numeric_limits<double>::quiet_NaN());
        if (physical_velocity)
            for (auto& component : *physical_velocity)
                component.assign(total_size, std::numeric_limits<double>::quiet_NaN());
        if (semantics != GridMetrics::GeometrySemantics::Existing &&
            semantics != GridMetrics::GeometrySemantics::AxisymmetricRz)
            throw std::invalid_argument("Unknown AMR thermodynamic geometry semantics");
        const bool native = semantics == GridMetrics::GeometrySemantics::AxisymmetricRz;
        if (native) {
            const int nx=grid.GetTotalX(),ny=grid.GetTotalY(),nz=grid.GetTotalZ();
            if (!arch::state::valid_bounds(bounds) || n_species<0 || total_size<=0
                || total_size!=grid.GetTotalSize() || state.block_total_size_!=total_size
                || nx<3 || nx>grid.stride_y || ny<=0 || nz!=1
                || grid.stride_z!=grid.stride_y*ny
                || static_cast<std::size_t>(n_species)>
                    std::numeric_limits<std::size_t>::max()/static_cast<std::size_t>(total_size))
                throw std::logic_error("Native RZ refinement logical layout/bounds mismatch");
            for (const auto* values : {&state.rho,&state.mom_u,&state.mom_v,
                                      &state.mom_w,&state.eng,&state.enuc_rate})
                if (values->size()!=static_cast<std::size_t>(total_size))
                    throw std::logic_error("Native RZ refinement field extent mismatch");
            if (state.mass_fractions.size()!=static_cast<std::size_t>(total_size)*
                static_cast<std::size_t>(n_species))
                throw std::logic_error("Native RZ refinement species extent mismatch");
        }
        std::vector<double> Xi(n_species, 0.0);
        const auto evaluate = [&] {
            if (native) {
                const auto geometry=GridMetrics::make_geometry_view(grid,semantics);
                const auto read=[&state](int index) {return state.get(index);};
                // Storage pitch padding is not a physical cell. Only genuine
                // logical ghost/interior cells may supply rho support or EOS.
                for (int k=0;k<grid.GetTotalZ();++k)
                    for (int j=0;j<grid.GetTotalY();++j)
                        for (int i=0;i<grid.GetTotalX();++i) {
                            const int index=grid.GetIndex(i,j,k);
                            for (int species=0;species<n_species;++species)
                                Xi[species]=state.X(species,index);
                            const auto closure=RzThermodynamics::make_cell_supported(
                                read,index,geometry,i,std::clamp(i-1,0,grid.GetTotalX()-3),bounds);
                            if (!closure.valid() || arch::state::validate_eos(
                                closure.effective_mean,Xi.data(),n_species,bounds,eos)
                                    !=arch::state::Status::valid)
                                throw std::runtime_error("Native RZ refinement rejected actual mean EOS closure");
                            const auto values=amr::indicator::thermodynamics(
                                closure.effective_mean,Xi.data(),eos,
                                pressure!=nullptr,temperature!=nullptr,gamma1!=nullptr);
                            if (pressure) (*pressure)[index]=values.pressure;
                            if (temperature) (*temperature)[index]=values.temperature;
                            if (gamma1) (*gamma1)[index]=values.gamma1;
                            if (physical_velocity) {
                                const auto point=RzThermodynamics::base_point(
                                    closure,geometry.GetCellCenterX(i));
                                if (arch::state::validate_eos(point,Xi.data(),n_species,bounds,eos)
                                    !=arch::state::Status::valid)
                                    throw std::runtime_error("Native RZ refinement rejected actual center EOS state");
                                (*physical_velocity)[0][index]=point.mom_u/point.rho;
                                (*physical_velocity)[1][index]=point.mom_v/point.rho;
                                (*physical_velocity)[2][index]=point.mom_w/point.rho;
                                for (const auto& component : *physical_velocity)
                                    if (!std::isfinite(component[index]))
                                        throw std::runtime_error("Native RZ refinement center velocity is not representable");
                            }
                        }
                return;
            }
            for (int index = 0; index < total_size; ++index) {
                for (int species = 0; species < n_species; ++species)
                    Xi[species] = state.X(species, index);
                const auto values = amr::indicator::thermodynamics(
                    state.get(index), Xi.data(), eos,
                    pressure != nullptr, temperature != nullptr, gamma1 != nullptr);
                if (pressure) (*pressure)[index] = values.pressure;
                if (temperature) (*temperature)[index] = values.temperature;
                if (gamma1) (*gamma1)[index] = values.gamma1;
            }
        };
        if constexpr (requires { typename EosPolicy::HostHydroScope; }) {
            // This callback borrows a fixed EOS and immutable patch. Its
            // exact inverse workspace expires before the next AMR callback;
            // failed roots and changed composition/energy are never reused.
            typename EosPolicy::HostHydroScope inverse_workspace(eos);
            evaluate();
        } else evaluate();
    });
}
} // namespace amr
