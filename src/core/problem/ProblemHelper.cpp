/**
 * @file ProblemHelper.cpp
 * @brief Problem Helper in the problem owner.
 *
 * Workflow:
 * 1. Receive a registered case and resolved configuration.
 * 2. Run setup and convert case initial fields with shared helpers.
 * 3. Return a valid species registry and initial state to Driver.
 */

#include <exception>
/**
 * @file ProblemHelper.cpp
 * @brief Shared problem setup, EOS queries and host-state initialization.
 *
 * Registered network metadata supplies species and initial fractions. Problem
 * callbacks provide primitive values; PopulateState traverses host AMR blocks
 * and uses the selected common EOS to construct their conservative state.
 */

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

#include "core/problem/ProblemHelper.h"
#include "core/problem/InitialStateConversion.h"

#include "amr/AMRControl.h"
#include "amr/topology/AmrDefines.h"
#include "data/GlobalDefs.h"
#include "core/config/ConfigValidation.h"
#include "data/UserTypes.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "interface/ProblemGenerator.h"
#include "numerics/burnsolver/Networks.h"
#include "physics/eos/eos_Utils.h"
#include "physics/eos/eosdispatch.h"
#include "physics/species/Species.h"

namespace ProblemHelper
{
    static void SetupNetwork(SimConfig &config, SpeciesManager &specs,
                             std::vector<double>* fractions)
    {
        config.RequireLoadedValues();
        using namespace arch::dispatch;
        const std::string &net_type = config.physics.burn.network_name;
        const auto selected = parse_registered_policy<NetworkPolicies>(
            net_type);

        if (!selected.ok)
            throw std::runtime_error(
                "Unknown network_name in SetupNetworkAndFractions: " + net_type);
        if (selected.value == NetworkId::None) {
            if (config.physics.burn.use_burn)
                throw std::runtime_error("Burning requires a registered reaction network");
            // A nonreacting problem supplies its own gas species and fractions.
            return;
        }
        bool setup = false;
        const bool registered = visit_policy<NetworkPolicies>(
            selected.value, [&]<class Registration> {
                using Binding =
                    typename PolicyRegistration<Registration>::CpuBinding;
                if constexpr (!std::is_same_v<Binding, AbsentBinding>
                              && !std::is_same_v<Binding,
                                                 CpuNoNetworkBinding>) {
                    using Network = typename CpuNetworkType<Binding>::type;
                    if (specs.count() == 0) Network::RegisterSpecies(specs);
                    if (fractions) Network::SetupInitialFractions(
                        config, specs, *fractions);
                    setup = true;
                }
            });
        if (!registered || !setup)
            throw std::logic_error("registered network has no CPU setup binding");
    }

    void SetupNetworkSpecies(SimConfig& config, SpeciesManager& specs)
    {
        SetupNetwork(config, specs, nullptr);
    }

    void SetupNetworkAndFractions(SimConfig& config, SpeciesManager& specs,
                                  std::vector<double>& fractions)
    {
        SetupNetwork(config, specs, &fractions);
    }

    double GetPressureFromRhoT(const SimConfig &config, const SpeciesManager &specs, double rho, double T, const double *X)
    {
        double p_out = 0.0;
        try {
            EOSDispatcher::dispatch_eos(config, specs, [&](auto &&eos) {
                p_out = eos.get_pressure_from_rho_T(rho, T, X);
            });
        } catch (const std::exception &error) {
            throw InitialEosError(error.what());
        }
        return p_out;
    }

    double GetRootCellWidth(const SimConfig &config, int logical_axis)
    {
        double lower = 0.0;
        double upper = 0.0;
        int root_blocks = 0;
        int active_cells_per_block = 0;

        switch (logical_axis) {
        case 1:
            lower = config.grid.x1_min;
            upper = config.grid.x1_max;
            root_blocks = config.grid.nblockx1;
            active_cells_per_block = amr::BLOCK_NX;
            break;
        case 2:
            if (config.grid.dim < 2) {
                throw std::invalid_argument("Root cell width requested for inactive x2 axis.");
            }
            lower = config.grid.x2_min;
            upper = config.grid.x2_max;
            root_blocks = config.grid.nblockx2;
            active_cells_per_block = amr::BLOCK_NY;
            break;
        case 3:
            if (config.grid.dim < 3) {
                throw std::invalid_argument("Root cell width requested for inactive x3 axis.");
            }
            lower = config.grid.x3_min;
            upper = config.grid.x3_max;
            root_blocks = config.grid.nblockx3;
            active_cells_per_block = amr::BLOCK_NZ;
            break;
        default:
            throw std::invalid_argument("Root cell width logical_axis must be 1, 2, or 3.");
        }

        if (root_blocks <= 0 || !std::isfinite(lower) || !std::isfinite(upper) || upper <= lower) {
            throw std::invalid_argument("Root cell width requires a positive block count and domain extent.");
        }

        return (upper - lower) /
            (static_cast<double>(root_blocks) * active_cells_per_block);
    }

    IsentropicState GetIsentropicStateAtPressureFactor(
        const SimConfig &config, const SpeciesManager &specs,
        double reference_rho, double reference_temperature,
        const double *X, double pressure_factor)
    {
        if (!std::isfinite(reference_rho) || reference_rho <= 0.0 ||
            !std::isfinite(reference_temperature) || reference_temperature <= 0.0 ||
            !std::isfinite(pressure_factor) || pressure_factor <= 0.0) {
            throw std::invalid_argument("Invalid reference state or pressure factor for isentropic initialization.");
        }

        IsentropicState result{};
        EOSDispatcher::dispatch_eos(config, specs, [&](auto &&eos) {
            const eos_utils::IsentropicState state =
                eos_utils::get_isentropic_state_at_pressure_factor(
                    eos, reference_rho, reference_temperature, X, pressure_factor);
            result.rho = state.rho;
            result.temperature = state.temperature;
            result.pressure = state.pressure;
            result.sound_speed = state.sound_speed;
        });
        return result;
    }

    namespace detail
    {
    void PopulateState(amr::AMRControl &amr_ctrl, const SimConfig &config,
                       const SpeciesManager &specs,
                       ProblemInitializationContext context,
                       std::function<void(const PointCoords&, PrimitiveData&)> init_callback)
    {
        arch::config::ValidateControls(config, specs.count());
        int n_species = specs.count();
        const auto& active_blocks = amr_ctrl.tree->GetActiveBlocks();
        // Validate the whole chart before invoking model callbacks or mutating
        // any block. Public callers retain Existing until full RZ migration.
        const auto semantics = context.geometry_semantics;
        if (semantics != GridMetrics::GeometrySemantics::Existing &&
            semantics != GridMetrics::GeometrySemantics::AxisymmetricRz)
            throw std::invalid_argument("Unknown initialization geometry profile");
        if (semantics == GridMetrics::GeometrySemantics::AxisymmetricRz &&
            (config.grid.dim != 2 || config.grid.geometry != "cylindrical"))
            throw std::invalid_argument("RZ initialization requires cylindrical dimension 2");
        for (int id : active_blocks) {
            const auto& block = amr_ctrl.pool->GetBlock(id);
            const auto& grid = block.grid;
            if (semantics==GridMetrics::GeometrySemantics::AxisymmetricRz) {
                const auto& state=block.fluid_state;
                const size_t size=grid.GetTotalSize();
                if(state.GetNumSpecies()!=n_species || state.rho.size()!=size
                    || state.mom_u.size()!=size || state.mom_v.size()!=size
                    || state.mom_w.size()!=size || state.eng.size()!=size
                    || state.enuc_rate.size()!=size
                    || state.mass_fractions.size()!=size*static_cast<size_t>(n_species))
                    throw std::invalid_argument("RZ initialization state/species layout mismatch");
            }
            if (grid.dim != config.grid.dim || grid.geometry != config.grid.geometry)
                throw std::invalid_argument("Initialization native grid/config geometry mismatch");
            (void)GridMetrics::make_geometry_view(grid, semantics);
        }

        // RZ cell averages are transactional: an unresolved rotating candidate
        // must not leave earlier blocks/cells or repair evidence partially live.
        // Physical ghost fill belongs to Driver boundary/exchange ownership.
        const bool rz=semantics==GridMetrics::GeometrySemantics::AxisymmetricRz;
        std::vector<FluidState> proposed;
        if (rz) {
            proposed.reserve(active_blocks.size());
            for (int id:active_blocks) {
                proposed.push_back(amr_ctrl.pool->GetBlock(id).fluid_state);
                auto& state=proposed.back();
                state.stage_repairs.reset(n_species,arch::state::RepairSemantics::RzVolumeAngular);
                std::fill(state.rho.begin(),state.rho.end(),arch::state::invalid());
                std::fill(state.mom_u.begin(),state.mom_u.end(),arch::state::invalid());
                std::fill(state.mom_v.begin(),state.mom_v.end(),arch::state::invalid());
                std::fill(state.mom_w.begin(),state.mom_w.end(),arch::state::invalid());
                std::fill(state.eng.begin(),state.eng.end(),arch::state::invalid());
                std::fill(state.mass_fractions.begin(),state.mass_fractions.end(),arch::state::invalid());
                std::fill(state.enuc_rate.begin(),state.enuc_rate.end(),0.);
            }
        }

        EOSDispatcher::dispatch_eos(context.eos, config, specs, [&](auto &&eos) {
            std::exception_ptr initialization_failure;
#pragma omp parallel
            {
                PrimitiveData data{};
                data.mass_fractions.resize(n_species, 0.0);

#pragma omp for schedule(dynamic)
                for (size_t b_idx = 0; b_idx < active_blocks.size(); ++b_idx)
                {
                    try {
                    amr::Block& b = amr_ctrl.pool->GetBlock(active_blocks[b_idx]);
                    if (rz) {
                        auto& output=proposed[b_idx];
                        for(int k=b.grid.Ks();k<b.grid.Ke();++k)
                        for(int j=b.grid.Js();j<b.grid.Je();++j)
                        for(int i=b.grid.Is();i<b.grid.Ie();++i) {
                            const double zlo=b.grid.GetAxialFacePosL(j);
                            const double zhi=b.grid.GetAxialFacePosR(j);
                            const auto cell=InitialRzCellState(b.grid.GetFacePosL(i),
                                b.grid.GetFacePosR(i),zlo,zhi,n_species,eos,config.numerics,init_callback);
                            const int index=b.grid.GetIndex(i,j,k);
                            output.set(index,cell.conserved);
                            for(int sp=0;sp<n_species;++sp)output.X(sp,index)=cell.mass_fractions[sp];
                        }
                        continue;
                    }
                    b.fluid_state.stage_repairs.reset(n_species);

                    for (int k = 0; k < b.grid.GetTotalZ(); ++k) {
                        for (int j = 0; j < b.grid.GetTotalY(); ++j) {
                            for (int i = 0; i < b.grid.GetTotalX(); ++i) {
                                // Skip padding zone
                                if (i >= b.grid.GetTotalX()) continue;

                                int idx = b.grid.GetIndex(i, j, k);

                                // Compute logical physical coordinate (assuming center of cell)
                                PointCoords p = b.grid.GetPhysicalCoords(i, j, k, semantics);

                                data.rho = 0.0;
                                data.u = 0.0;
                                data.v = 0.0;
                                data.w = 0.0;
                                data.p = 0.0;
                                data.temperature = 0.0;
                                data.has_temperature = false;
                                std::fill(data.mass_fractions.begin(), data.mass_fractions.end(), 0.0);

                                init_callback(p, data);

                                arch::state::Repair repair;
                                const FluidVector state = InitialConservedState(data, eos, config.numerics, &repair);
                                if (repair.status == arch::state::Status::repaired
                                    && i >= b.grid.Is() && i < b.grid.Ie()
                                    && j >= b.grid.Js() && j < b.grid.Je()
                                    && k >= b.grid.Ks() && k < b.grid.Ke()) {
                                    const double volume=GridMetrics::CellVolume(GridMetrics::make_geometry_view(b.grid,semantics),i,j,k);
                                    if (b.fluid_state.stage_repairs.values[0] == 0.0) {
                                        b.fluid_state.stage_repairs.position[0]=p.x;
                                        b.fluid_state.stage_repairs.position[1]=p.y;
                                        b.fluid_state.stage_repairs.position[2]=p.z;
                                    }
                                    auto report=b.fluid_state.stage_repairs.view();
                                    report.event(volume,idx);
                                    const auto delta=volume*repair.delta;
                                    report.conserved(delta.rho,delta.mom_u,delta.mom_v,delta.mom_w,delta.eng);
                                    for (int sp=0;sp<n_species;++sp) report.species_mass(sp,delta.rho*data.mass_fractions[sp]);
                                }
                                b.fluid_state.rho[idx] = state.rho;
                                b.fluid_state.mom_u[idx] = state.mom_u;
                                b.fluid_state.mom_v[idx] = state.mom_v;
                                b.fluid_state.mom_w[idx] = state.mom_w;
                                b.fluid_state.eng[idx] = state.eng;

                                for (int s = 0; s < n_species; ++s)
                                    b.fluid_state.X(s, idx) = data.mass_fractions[s];
                            }
                        }
                    }
                    } catch (...) {
#pragma omp critical(arch_initialization_failure)
                        { if (!initialization_failure) initialization_failure=std::current_exception(); }
                    }
                }
            }
            if (initialization_failure) std::rethrow_exception(initialization_failure);
        });
        if (rz) {
            static_assert(std::is_nothrow_swappable_v<FluidState>);
            for(size_t index=0;index<active_blocks.size();++index)
                std::swap(amr_ctrl.pool->GetBlock(active_blocks[index]).fluid_state,proposed[index]);
        }
    }
    } // namespace detail
} // namespace ProblemHelper
