/**
 * Real table-domain failures in an uncommitted ODE state. A test-only linear
 * response injects one oversized thermal correction; the original method must
 * reject it, then solve the same analytic no-heat transfer with smaller steps.
 * Host and CUDA run this same continuation and exact table queries.
 */
#pragma once

#include "fixtures/burn/SparseTransferNetwork.h"
#include "numerics/burnsolver/ode/ode_be-nr.h"
#include "numerics/burnsolver/ode/ode_bd.h"
#include "numerics/burnsolver/ode/ode_ros4.h"
#include "numerics/linalg/DenseWrap.h"

namespace arch::test {
struct TabularTrialResult {
    bool complete = false, rejected = false, unchanged_temperature = false;
    double species_error = 0.0;
};

template<template<class, class, class> class Method, class Eos>
ARCH_HOST_DEVICE TabularTrialResult tabular_trial_recovery(Eos eos, bool inject)
{
    using Matrix = DenseMatrixData<3>;
    using Solver = Method<SparseTransferNetwork<2>, Matrix, DenseLUSolver>;
    BurnConfigView controls{};
    controls.use_burn = true;
    controls.use_nse = false;
    controls.smallt = 1.0;
    controls.smallx = 1e-30;
    controls.nuclearTempMin = controls.nuclearDensMin = 0.0;
    // Numeric view has no defaults; use the complete maintained controller.
    controls.odeconfig = {1e-7, 1e-10, 50, 10000, .9, 2.0, .1, 1.0};
    constexpr double interval = .1, density = 1.5, temperature = 1.4;
    double state[]{.8, .2, temperature};
    typename Solver::Continuation continuation;
    Matrix jacobian, system;
    int pivots[3];
    Solver::begin(continuation, state, density, interval, controls, interval);
    bool injected = false;
    for (int request_count = 0; request_count < 100000; ++request_count) {
        const auto request = Solver::advance(continuation, jacobian, system, state, eos, controls);
        if (request == OdeLinearRequest::Complete) break;
        bool solved = true;
        if (request == OdeLinearRequest::FactorizeAndSolve)
            solved = DenseLUSolver::solve<3, 3>(system, continuation.b);
        else if (request == OdeLinearRequest::Factorize)
            solved = DenseLUSolver::factorize<3, 3>(system, pivots);
        else DenseLUSolver::solve_with_factors<3, 3>(system, pivots, continuation.b);
        if (inject && !injected && solved && request != OdeLinearRequest::Factorize) {
            // This is below the ODE temperature upper guard but far outside
            // the real table. A mere arithmetic/admissibility guard cannot pass.
            continuation.b[2] = 10.0;
            injected = true;
        }
        Solver::complete_linear_solve(continuation, solved);
    }
    return {continuation.report.success() && continuation.t_current == interval,
            continuation.report.rejected_substeps > 0,
            std::abs(state[2] - temperature) < 1e-11,
            std::abs(state[0] - .8 * std::exp(-.02 * interval))};
}
} // namespace arch::test

// Manufactured equilibrium projection for the *thermal corrector* contract.
// Composition/heat vary linearly with T and give an independently evaluable
// scalar root. Actual Saha and nuclear data remain covered by generated-NSE tests.
namespace arch::test {
struct TrialNseNetwork {
    static constexpr int NUM_SPECIES=2, ODE_NEQ=3;
    ARCH_HOST_DEVICE static double aion(int i) { return i==0 ? 2. : 4.; }
    ARCH_HOST_DEVICE static double zion(int i) { return i==0 ? 1. : 2.; }
};
}
template<> struct NSESolver<arch::test::TrialNseNetwork> {
    static constexpr bool generated_data=false;
    ARCH_HOST_DEVICE static bool solve(double T, double, double, const double* old,
                                      double* composition, double& energy) {
        const double transfer=.02-.3*(T-2.4);
        composition[0]=old[0]-transfer;
        composition[1]=old[1]+transfer;
        energy=10.*transfer;
        return composition[0]>0. && composition[1]>0.;
    }
};
namespace arch::test {
template<class View> struct ObservedTrialView : View {
    int* out_of_table=nullptr;
    ARCH_HOST_DEVICE double get_eint_from_T(double rho,double T,const double* x) const {
        if (T>std::pow(10.,this->log_T_max)) ++*out_of_table;
        return View::get_eint_from_T(rho,T,x);
    }
};

template<class Eos>
ARCH_HOST_DEVICE bool tabular_nse_trial_recovery(Eos eos)
{
    int outside=0;
    ObservedTrialView<Eos> observed;
    static_cast<Eos&>(observed)=eos;
    observed.out_of_table=&outside;
    double state[]{.8,.2,2.4};
    BurnConfigView cfg{};
    cfg.use_nse=true; cfg.smallt=cfg.nseTempThreshold=1.;
    double recommended=.1, energy=0.;
    const double initial=eos.get_eint_from_T(1.5,state[2],state);
    const bool solved=OdeMath::integrate_nse_state<TrialNseNetwork>(
        state,1.5,.1,observed,cfg,recommended,&energy);
    const double after=eos.get_eint_from_T(1.5,state[2],state);
    return solved && outside>0 && state[2]>2.4 && state[2]<2.512
        && std::abs(after-initial-(.2-3.*(state[2]-2.4)))<2e-10
        && std::abs(energy-(.2-3.*(state[2]-2.4)))<1e-14
        && std::abs(state[0]+state[1]-1.)<1e-14;
}
} // namespace arch::test
