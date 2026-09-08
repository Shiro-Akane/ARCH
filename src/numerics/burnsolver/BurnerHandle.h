/**
 * @file BurnerHandle.h
 * @brief Host burner binding that isolates ODE policy templates from hydro dispatch.
 */
#pragma once

#include <type_traits>

#include "../../data/GlobalDefs.h"

/**
 * @brief Coarse-grained, host-only type erasure for one configured CPU burner.
 *
 * The indirect call occurs once per burning cell, outside the individual
 * network-rate and dense-LU loops.  This keeps the hot implementation fully
 * templated while preventing every network/ODE/linear-solver choice from
 * multiplying every hydro/integrator/reconstruction instantiation.
 */
template <typename EosPolicy>
class BurnerHandle
{
public:
    using IntegrateFn = bool (*)(double *, double, double,
                                 const EosPolicy &, const BurnConfig &, double &, double *);

    BurnerHandle() noexcept = default;

    template <typename BurnerPolicy>
    static BurnerHandle bind()
    {
        static_assert(std::is_empty_v<BurnerPolicy>,
                      "BurnerHandle currently requires a stateless burner policy");
        static_assert(std::is_default_constructible_v<BurnerPolicy>,
                      "BurnerHandle requires a default-constructible burner policy");

        BurnerHandle handle;
        handle.integrate_ = &integrate_thunk<BurnerPolicy>;
        if constexpr (requires { BurnerPolicy::NEQ; })
            handle.state_size_ = BurnerPolicy::NEQ;
        return handle;
    }

    // Exact packed extent belongs to the selected ODE, not to the grid's
    // species count. Disabled/unbound/CUDA-host guards have no CPU state.
    int state_size() const noexcept { return state_size_; }

    bool integrate(double *state, double rho, double dt_target,
                   const EosPolicy &eos, const BurnConfig &config,
                   double &dt_recommended, double *energy_change = nullptr) const
    {
        return integrate_(state, rho, dt_target, eos, config, dt_recommended, energy_change);
    }

private:
    template <typename BurnerPolicy>
    static bool integrate_thunk(double *state, double rho, double dt_target,
                                const EosPolicy &eos, const BurnConfig &config,
                                double &dt_recommended, double *energy_change)
    {
        const BurnerPolicy burner{};
        return burner.integrate(state, rho, dt_target, eos, config,
                                dt_recommended, energy_change);
    }

    static bool unbound(double *, double, double, const EosPolicy &,
                        const BurnConfig &, double &, double *) noexcept
    {
        return false;
    }

    IntegrateFn integrate_ = &unbound;
    int state_size_ = 0;
};
