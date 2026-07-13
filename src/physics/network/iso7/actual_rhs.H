#ifndef actual_rhs_H
#define actual_rhs_H

#include <amrex_bridge.H>

#include <actual_network.H>
#include <burn_type.H>
#include <screen.H>
#include <rate_type.H>
#include <approximate_rates.H>
#include <modified_rates.H>
#include <reaclib_rates.H>
#include <derived_rates.H>
#include <table_rates.H>

using namespace Species;
using namespace Rates;

using namespace rate_tables;

template<class T>
inline
void ener_gener_rate(T const& dydt, Real& enuc)
{

    // Computes the instantaneous energy generation rate (from the nuclei)

    // This is basically e = m c**2

    enuc = 0.0_rt;

    for (int n = 1; n <= NumSpec; ++n) {
        enuc += dydt(n) * network::mion(n);
    }

    enuc *= C::enuc_conv2;
}


#ifdef SCREENING
template <int do_T_derivatives, typename T>
inline
void evaluate_screening(const burn_t& state,
                        const Array1D<Real, 1, NumSpec>& Y,
                        T& rate_eval) {

    // Evaluating screening factors

    plasma_state_t pstate{};
    fill_plasma_state(pstate, state.T, state.rho, Y);

    Real log_scor;

    {
        auto scn_fac = scrn::calculate_screen_factor(2.0_rt, 4.0_rt, 6.0_rt, 12.0_rt);
        actual_log_screen(pstate, scn_fac, log_scor);
        rate_eval.log_screen(k_He4_C12) = log_scor;
    }

    {
        auto scn_fac = scrn::calculate_screen_factor(6.0_rt, 12.0_rt, 6.0_rt, 12.0_rt);
        actual_log_screen(pstate, scn_fac, log_scor);
        rate_eval.log_screen(k_C12_C12) = log_scor;
    }

    {
        auto scn_fac = scrn::calculate_screen_factor(2.0_rt, 4.0_rt, 2.0_rt, 4.0_rt);
        actual_log_screen(pstate, scn_fac, log_scor);
        rate_eval.log_screen(k_He4_He4) = log_scor;
    }

    {
        auto scn_fac = scrn::calculate_screen_factor(2.0_rt, 4.0_rt, 8.0_rt, 16.0_rt);
        actual_log_screen(pstate, scn_fac, log_scor);
        rate_eval.log_screen(k_He4_O16) = log_scor;
    }

    {
        auto scn_fac = scrn::calculate_screen_factor(2.0_rt, 4.0_rt, 10.0_rt, 20.0_rt);
        actual_log_screen(pstate, scn_fac, log_scor);
        rate_eval.log_screen(k_He4_Ne20) = log_scor;
    }

    {
        auto scn_fac = scrn::calculate_screen_factor(2.0_rt, 4.0_rt, 12.0_rt, 24.0_rt);
        actual_log_screen(pstate, scn_fac, log_scor);
        rate_eval.log_screen(k_He4_Mg24) = log_scor;
    }

    {
        auto scn_fac = scrn::calculate_screen_factor(6.0_rt, 12.0_rt, 8.0_rt, 16.0_rt);
        actual_log_screen(pstate, scn_fac, log_scor);
        rate_eval.log_screen(k_C12_O16) = log_scor;
    }

    {
        auto scn_fac = scrn::calculate_screen_factor(6.0_rt, 12.0_rt, 10.0_rt, 20.0_rt);
        actual_log_screen(pstate, scn_fac, log_scor);
        rate_eval.log_screen(k_C12_Ne20) = log_scor;
    }

    {
        auto scn_fac = scrn::calculate_screen_factor(2.0_rt, 4.0_rt, 14.0_rt, 28.0_rt);
        actual_log_screen(pstate, scn_fac, log_scor);
        rate_eval.log_screen(k_He4_Si28) = log_scor;
    }

    {
        auto scn_fac = scrn::calculate_screen_factor(8.0_rt, 16.0_rt, 8.0_rt, 16.0_rt);
        actual_log_screen(pstate, scn_fac, log_scor);
        rate_eval.log_screen(k_O16_O16) = log_scor;
    }

    {
        auto scn_fac = scrn::calculate_screen_factor(2.0_rt, 4.0_rt, 4.0_rt, 8.0_rt);
        actual_log_screen(pstate, scn_fac, log_scor);
        rate_eval.log_screen(k_He4_Be8) = log_scor;
    }

}
#endif


template <int do_T_derivatives, typename T>
inline
void evaluate_rates(const burn_t& state, T& rate_eval) {

    // create molar fractions

    Array1D<Real, 1, NumSpec> Y;
    for (int n = 1; n <= NumSpec; ++n) {
        Y(n) = state.xn[n-1] * aion_inv[n-1];
    }

    assert(state.y_e != 0.0);

    [[maybe_unused]] Real rhoy = state.rho * state.y_e;

    // Precompute screening terms

#ifdef SCREENING
    evaluate_screening<do_T_derivatives, T>(state, Y, rate_eval);
#endif

    tf_t tfactors = evaluate_tfactors(state.T);

    // Calculate Reaclib rates

    fill_reaclib_rates<do_T_derivatives, T>(tfactors, rate_eval);

    // fill modified rates next -- these can have ReacLib or
    // TemperatureTabular/StarLib rates as the original rate
    modified_rates::fill_rates<do_T_derivatives, T>(tfactors, rate_eval);

    // Calculate Derived Rates. This should go last but before approx rates.

    fill_derived_rates<do_T_derivatives, T>(tfactors, rate_eval);

    // Fill approximate rates

    fill_approx_rates<do_T_derivatives, T>(tfactors, state.rho, Y, rate_eval);

    // Calculate tabular rates

    [[maybe_unused]] Real rate, drate_dt, edot_nu, edot_gamma;

    rate_eval.enuc_weak = 0.0_rt;


}

inline
void rhs_nuc(const burn_t& state,
             Array1D<Real, 1, NumSpec>& ydot_nuc,
             const Array1D<Real, 1, NumSpec>& Y,
             const Array1D<Real, 1, NumRates>& screened_rates) {

    using namespace Rates;

    ydot_nuc(He4) =
        (-screened_rates(k_He4_C12_to_O16_reaclib)*Y(C12)*Y(He4)*state.rho + screened_rates(k_O16_to_He4_C12_reaclib)*Y(O16)) +
        (-screened_rates(k_He4_O16_to_Ne20_reaclib)*Y(He4)*Y(O16)*state.rho + screened_rates(k_Ne20_to_He4_O16_reaclib)*Y(Ne20)) +
        (-screened_rates(k_He4_Ne20_to_Mg24_reaclib)*Y(He4)*Y(Ne20)*state.rho + screened_rates(k_Mg24_to_He4_Ne20_reaclib)*Y(Mg24)) +
        (-screened_rates(k_He4_Mg24_to_Si28_reaclib)*Y(He4)*Y(Mg24)*state.rho + screened_rates(k_Si28_to_He4_Mg24_reaclib)*Y(Si28)) +
        (0.5*screened_rates(k_C12_C12_to_He4_Ne20_reaclib)*std::pow(Y(C12), 2)*state.rho + -screened_rates(k_He4_Ne20_to_C12_C12_reaclib)*Y(He4)*Y(Ne20)*state.rho) +
        (screened_rates(k_C12_O16_to_He4_Mg24_reaclib)*Y(C12)*Y(O16)*state.rho + -screened_rates(k_He4_Mg24_to_C12_O16_reaclib)*Y(He4)*Y(Mg24)*state.rho) +
        (0.5*screened_rates(k_O16_O16_to_He4_Si28_reaclib)*std::pow(Y(O16), 2)*state.rho + -screened_rates(k_He4_Si28_to_O16_O16_reaclib)*Y(He4)*Y(Si28)*state.rho) +
        (screened_rates(k_C12_Ne20_to_He4_Si28_reaclib)*Y(C12)*Y(Ne20)*state.rho + -screened_rates(k_He4_Si28_to_C12_Ne20_reaclib)*Y(He4)*Y(Si28)*state.rho) +
        (-0.5*screened_rates(k_He4_He4_He4_to_C12_reaclib)*std::pow(Y(He4), 3)*std::pow(state.rho, 2) + 3.0*screened_rates(k_C12_to_He4_He4_He4_reaclib)*Y(C12));

    ydot_nuc(C12) =
        (-screened_rates(k_He4_C12_to_O16_reaclib)*Y(C12)*Y(He4)*state.rho + screened_rates(k_O16_to_He4_C12_reaclib)*Y(O16)) +
        (-screened_rates(k_C12_C12_to_He4_Ne20_reaclib)*std::pow(Y(C12), 2)*state.rho + 2.0*screened_rates(k_He4_Ne20_to_C12_C12_reaclib)*Y(He4)*Y(Ne20)*state.rho) +
        (-screened_rates(k_C12_O16_to_He4_Mg24_reaclib)*Y(C12)*Y(O16)*state.rho + screened_rates(k_He4_Mg24_to_C12_O16_reaclib)*Y(He4)*Y(Mg24)*state.rho) +
        (-screened_rates(k_C12_Ne20_to_He4_Si28_reaclib)*Y(C12)*Y(Ne20)*state.rho + screened_rates(k_He4_Si28_to_C12_Ne20_reaclib)*Y(He4)*Y(Si28)*state.rho) +
        (0.16666666666666667*screened_rates(k_He4_He4_He4_to_C12_reaclib)*std::pow(Y(He4), 3)*std::pow(state.rho, 2) + -screened_rates(k_C12_to_He4_He4_He4_reaclib)*Y(C12));

    ydot_nuc(O16) =
        (screened_rates(k_He4_C12_to_O16_reaclib)*Y(C12)*Y(He4)*state.rho + -screened_rates(k_O16_to_He4_C12_reaclib)*Y(O16)) +
        (-screened_rates(k_He4_O16_to_Ne20_reaclib)*Y(He4)*Y(O16)*state.rho + screened_rates(k_Ne20_to_He4_O16_reaclib)*Y(Ne20)) +
        (-screened_rates(k_C12_O16_to_He4_Mg24_reaclib)*Y(C12)*Y(O16)*state.rho + screened_rates(k_He4_Mg24_to_C12_O16_reaclib)*Y(He4)*Y(Mg24)*state.rho) +
        (-screened_rates(k_O16_O16_to_He4_Si28_reaclib)*std::pow(Y(O16), 2)*state.rho + 2.0*screened_rates(k_He4_Si28_to_O16_O16_reaclib)*Y(He4)*Y(Si28)*state.rho);

    ydot_nuc(Ne20) =
        (screened_rates(k_He4_O16_to_Ne20_reaclib)*Y(He4)*Y(O16)*state.rho + -screened_rates(k_Ne20_to_He4_O16_reaclib)*Y(Ne20)) +
        (-screened_rates(k_He4_Ne20_to_Mg24_reaclib)*Y(He4)*Y(Ne20)*state.rho + screened_rates(k_Mg24_to_He4_Ne20_reaclib)*Y(Mg24)) +
        (0.5*screened_rates(k_C12_C12_to_He4_Ne20_reaclib)*std::pow(Y(C12), 2)*state.rho + -screened_rates(k_He4_Ne20_to_C12_C12_reaclib)*Y(He4)*Y(Ne20)*state.rho) +
        (-screened_rates(k_C12_Ne20_to_He4_Si28_reaclib)*Y(C12)*Y(Ne20)*state.rho + screened_rates(k_He4_Si28_to_C12_Ne20_reaclib)*Y(He4)*Y(Si28)*state.rho);

    ydot_nuc(Mg24) =
        (screened_rates(k_He4_Ne20_to_Mg24_reaclib)*Y(He4)*Y(Ne20)*state.rho + -screened_rates(k_Mg24_to_He4_Ne20_reaclib)*Y(Mg24)) +
        (-screened_rates(k_He4_Mg24_to_Si28_reaclib)*Y(He4)*Y(Mg24)*state.rho + screened_rates(k_Si28_to_He4_Mg24_reaclib)*Y(Si28)) +
        (screened_rates(k_C12_O16_to_He4_Mg24_reaclib)*Y(C12)*Y(O16)*state.rho + -screened_rates(k_He4_Mg24_to_C12_O16_reaclib)*Y(He4)*Y(Mg24)*state.rho);

    ydot_nuc(Si28) =
        (screened_rates(k_He4_Mg24_to_Si28_reaclib)*Y(He4)*Y(Mg24)*state.rho + -screened_rates(k_Si28_to_He4_Mg24_reaclib)*Y(Si28)) +
        (0.5*screened_rates(k_O16_O16_to_He4_Si28_reaclib)*std::pow(Y(O16), 2)*state.rho + -screened_rates(k_He4_Si28_to_O16_O16_reaclib)*Y(He4)*Y(Si28)*state.rho) +
        (screened_rates(k_C12_Ne20_to_He4_Si28_reaclib)*Y(C12)*Y(Ne20)*state.rho + -screened_rates(k_He4_Si28_to_C12_Ne20_reaclib)*Y(He4)*Y(Si28)*state.rho);

}


inline
void actual_rhs (burn_t& state, Array1D<Real, 1, NumSpec>& ydot, Real& enu_weak)
{

    compute_ye(state);

    for (int i = 1; i <= NumSpec; ++i) {
        ydot(i) = 0.0_rt;
    }


    // Set molar abundances
    Array1D<Real, 1, NumSpec> Y;
    for (int i = 1; i <= NumSpec; ++i) {
        Y(i) = state.xn[i-1] * aion_inv[i-1];
    }

    // build the rates

    rate_t rate_eval;

    constexpr int do_T_derivatives = 0;
    evaluate_rates<do_T_derivatives, rate_t>(state, rate_eval);

    rhs_nuc(state, ydot, Y, rate_eval.screened_rates);

    // enu_weak is the neutrino losses from the weak rates.  it is
    // already negative, so it should just be added to the energy from
    // the mass difference / composition changes.
    enu_weak = rate_eval.enuc_weak;

}


template<class MatrixType>
inline
void jac_nuc(const burn_t& state,
             MatrixType& jac,
             const Array1D<Real, 1, NumSpec>& Y,
             const Array1D<Real, 1, NumRates>& screened_rates)
{

    Real scratch;

    scratch = -screened_rates(k_He4_C12_to_O16_reaclib)*Y(C12)*state.rho - 1.5*screened_rates(k_He4_He4_He4_to_C12_reaclib)*std::pow(Y(He4), 2)*std::pow(state.rho, 2) - screened_rates(k_He4_Mg24_to_C12_O16_reaclib)*Y(Mg24)*state.rho - screened_rates(k_He4_Mg24_to_Si28_reaclib)*Y(Mg24)*state.rho - screened_rates(k_He4_Ne20_to_C12_C12_reaclib)*Y(Ne20)*state.rho - screened_rates(k_He4_Ne20_to_Mg24_reaclib)*Y(Ne20)*state.rho - screened_rates(k_He4_O16_to_Ne20_reaclib)*Y(O16)*state.rho - screened_rates(k_He4_Si28_to_C12_Ne20_reaclib)*Y(Si28)*state.rho - screened_rates(k_He4_Si28_to_O16_O16_reaclib)*Y(Si28)*state.rho;
    jac.set(He4, He4, scratch);

    scratch = 1.0*screened_rates(k_C12_C12_to_He4_Ne20_reaclib)*Y(C12)*state.rho + screened_rates(k_C12_Ne20_to_He4_Si28_reaclib)*Y(Ne20)*state.rho + screened_rates(k_C12_O16_to_He4_Mg24_reaclib)*Y(O16)*state.rho + 3.0*screened_rates(k_C12_to_He4_He4_He4_reaclib) - screened_rates(k_He4_C12_to_O16_reaclib)*Y(He4)*state.rho;
    jac.set(He4, C12, scratch);

    scratch = screened_rates(k_C12_O16_to_He4_Mg24_reaclib)*Y(C12)*state.rho - screened_rates(k_He4_O16_to_Ne20_reaclib)*Y(He4)*state.rho + 1.0*screened_rates(k_O16_O16_to_He4_Si28_reaclib)*Y(O16)*state.rho + screened_rates(k_O16_to_He4_C12_reaclib);
    jac.set(He4, O16, scratch);

    scratch = screened_rates(k_C12_Ne20_to_He4_Si28_reaclib)*Y(C12)*state.rho - screened_rates(k_He4_Ne20_to_C12_C12_reaclib)*Y(He4)*state.rho - screened_rates(k_He4_Ne20_to_Mg24_reaclib)*Y(He4)*state.rho + screened_rates(k_Ne20_to_He4_O16_reaclib);
    jac.set(He4, Ne20, scratch);

    scratch = -screened_rates(k_He4_Mg24_to_C12_O16_reaclib)*Y(He4)*state.rho - screened_rates(k_He4_Mg24_to_Si28_reaclib)*Y(He4)*state.rho + screened_rates(k_Mg24_to_He4_Ne20_reaclib);
    jac.set(He4, Mg24, scratch);

    scratch = -screened_rates(k_He4_Si28_to_C12_Ne20_reaclib)*Y(He4)*state.rho - screened_rates(k_He4_Si28_to_O16_O16_reaclib)*Y(He4)*state.rho + screened_rates(k_Si28_to_He4_Mg24_reaclib);
    jac.set(He4, Si28, scratch);

    scratch = -screened_rates(k_He4_C12_to_O16_reaclib)*Y(C12)*state.rho + 0.5*screened_rates(k_He4_He4_He4_to_C12_reaclib)*std::pow(Y(He4), 2)*std::pow(state.rho, 2) + screened_rates(k_He4_Mg24_to_C12_O16_reaclib)*Y(Mg24)*state.rho + 2.0*screened_rates(k_He4_Ne20_to_C12_C12_reaclib)*Y(Ne20)*state.rho + screened_rates(k_He4_Si28_to_C12_Ne20_reaclib)*Y(Si28)*state.rho;
    jac.set(C12, He4, scratch);

    scratch = -2.0*screened_rates(k_C12_C12_to_He4_Ne20_reaclib)*Y(C12)*state.rho - screened_rates(k_C12_Ne20_to_He4_Si28_reaclib)*Y(Ne20)*state.rho - screened_rates(k_C12_O16_to_He4_Mg24_reaclib)*Y(O16)*state.rho - screened_rates(k_C12_to_He4_He4_He4_reaclib) - screened_rates(k_He4_C12_to_O16_reaclib)*Y(He4)*state.rho;
    jac.set(C12, C12, scratch);

    scratch = -screened_rates(k_C12_O16_to_He4_Mg24_reaclib)*Y(C12)*state.rho + screened_rates(k_O16_to_He4_C12_reaclib);
    jac.set(C12, O16, scratch);

    scratch = -screened_rates(k_C12_Ne20_to_He4_Si28_reaclib)*Y(C12)*state.rho + 2.0*screened_rates(k_He4_Ne20_to_C12_C12_reaclib)*Y(He4)*state.rho;
    jac.set(C12, Ne20, scratch);

    scratch = screened_rates(k_He4_Mg24_to_C12_O16_reaclib)*Y(He4)*state.rho;
    jac.set(C12, Mg24, scratch);

    scratch = screened_rates(k_He4_Si28_to_C12_Ne20_reaclib)*Y(He4)*state.rho;
    jac.set(C12, Si28, scratch);

    scratch = screened_rates(k_He4_C12_to_O16_reaclib)*Y(C12)*state.rho + screened_rates(k_He4_Mg24_to_C12_O16_reaclib)*Y(Mg24)*state.rho - screened_rates(k_He4_O16_to_Ne20_reaclib)*Y(O16)*state.rho + 2.0*screened_rates(k_He4_Si28_to_O16_O16_reaclib)*Y(Si28)*state.rho;
    jac.set(O16, He4, scratch);

    scratch = -screened_rates(k_C12_O16_to_He4_Mg24_reaclib)*Y(O16)*state.rho + screened_rates(k_He4_C12_to_O16_reaclib)*Y(He4)*state.rho;
    jac.set(O16, C12, scratch);

    scratch = -screened_rates(k_C12_O16_to_He4_Mg24_reaclib)*Y(C12)*state.rho - screened_rates(k_He4_O16_to_Ne20_reaclib)*Y(He4)*state.rho - 2.0*screened_rates(k_O16_O16_to_He4_Si28_reaclib)*Y(O16)*state.rho - screened_rates(k_O16_to_He4_C12_reaclib);
    jac.set(O16, O16, scratch);

    scratch = screened_rates(k_Ne20_to_He4_O16_reaclib);
    jac.set(O16, Ne20, scratch);

    scratch = screened_rates(k_He4_Mg24_to_C12_O16_reaclib)*Y(He4)*state.rho;
    jac.set(O16, Mg24, scratch);

    scratch = 2.0*screened_rates(k_He4_Si28_to_O16_O16_reaclib)*Y(He4)*state.rho;
    jac.set(O16, Si28, scratch);

    scratch = -screened_rates(k_He4_Ne20_to_C12_C12_reaclib)*Y(Ne20)*state.rho - screened_rates(k_He4_Ne20_to_Mg24_reaclib)*Y(Ne20)*state.rho + screened_rates(k_He4_O16_to_Ne20_reaclib)*Y(O16)*state.rho + screened_rates(k_He4_Si28_to_C12_Ne20_reaclib)*Y(Si28)*state.rho;
    jac.set(Ne20, He4, scratch);

    scratch = 1.0*screened_rates(k_C12_C12_to_He4_Ne20_reaclib)*Y(C12)*state.rho - screened_rates(k_C12_Ne20_to_He4_Si28_reaclib)*Y(Ne20)*state.rho;
    jac.set(Ne20, C12, scratch);

    scratch = screened_rates(k_He4_O16_to_Ne20_reaclib)*Y(He4)*state.rho;
    jac.set(Ne20, O16, scratch);

    scratch = -screened_rates(k_C12_Ne20_to_He4_Si28_reaclib)*Y(C12)*state.rho - screened_rates(k_He4_Ne20_to_C12_C12_reaclib)*Y(He4)*state.rho - screened_rates(k_He4_Ne20_to_Mg24_reaclib)*Y(He4)*state.rho - screened_rates(k_Ne20_to_He4_O16_reaclib);
    jac.set(Ne20, Ne20, scratch);

    scratch = screened_rates(k_Mg24_to_He4_Ne20_reaclib);
    jac.set(Ne20, Mg24, scratch);

    scratch = screened_rates(k_He4_Si28_to_C12_Ne20_reaclib)*Y(He4)*state.rho;
    jac.set(Ne20, Si28, scratch);

    scratch = -screened_rates(k_He4_Mg24_to_C12_O16_reaclib)*Y(Mg24)*state.rho - screened_rates(k_He4_Mg24_to_Si28_reaclib)*Y(Mg24)*state.rho + screened_rates(k_He4_Ne20_to_Mg24_reaclib)*Y(Ne20)*state.rho;
    jac.set(Mg24, He4, scratch);

    scratch = screened_rates(k_C12_O16_to_He4_Mg24_reaclib)*Y(O16)*state.rho;
    jac.set(Mg24, C12, scratch);

    scratch = screened_rates(k_C12_O16_to_He4_Mg24_reaclib)*Y(C12)*state.rho;
    jac.set(Mg24, O16, scratch);

    scratch = screened_rates(k_He4_Ne20_to_Mg24_reaclib)*Y(He4)*state.rho;
    jac.set(Mg24, Ne20, scratch);

    scratch = -screened_rates(k_He4_Mg24_to_C12_O16_reaclib)*Y(He4)*state.rho - screened_rates(k_He4_Mg24_to_Si28_reaclib)*Y(He4)*state.rho - screened_rates(k_Mg24_to_He4_Ne20_reaclib);
    jac.set(Mg24, Mg24, scratch);

    scratch = screened_rates(k_Si28_to_He4_Mg24_reaclib);
    jac.set(Mg24, Si28, scratch);

    scratch = screened_rates(k_He4_Mg24_to_Si28_reaclib)*Y(Mg24)*state.rho - screened_rates(k_He4_Si28_to_C12_Ne20_reaclib)*Y(Si28)*state.rho - screened_rates(k_He4_Si28_to_O16_O16_reaclib)*Y(Si28)*state.rho;
    jac.set(Si28, He4, scratch);

    scratch = screened_rates(k_C12_Ne20_to_He4_Si28_reaclib)*Y(Ne20)*state.rho;
    jac.set(Si28, C12, scratch);

    scratch = 1.0*screened_rates(k_O16_O16_to_He4_Si28_reaclib)*Y(O16)*state.rho;
    jac.set(Si28, O16, scratch);

    scratch = screened_rates(k_C12_Ne20_to_He4_Si28_reaclib)*Y(C12)*state.rho;
    jac.set(Si28, Ne20, scratch);

    scratch = screened_rates(k_He4_Mg24_to_Si28_reaclib)*Y(He4)*state.rho;
    jac.set(Si28, Mg24, scratch);

    scratch = -screened_rates(k_He4_Si28_to_C12_Ne20_reaclib)*Y(He4)*state.rho - screened_rates(k_He4_Si28_to_O16_O16_reaclib)*Y(He4)*state.rho - screened_rates(k_Si28_to_He4_Mg24_reaclib);
    jac.set(Si28, Si28, scratch);


}



template<class MatrixType>
inline
void actual_jac(burn_t& state, MatrixType& jac)
{

    compute_ye(state);

    // Set molar abundances
    Array1D<Real, 1, NumSpec> Y;
    for (int i = 1; i <= NumSpec; ++i) {
        Y(i) = state.xn[i-1] * aion_inv[i-1];
    }


    jac.zero();

    rate_derivs_t rate_eval;

    constexpr int do_T_derivatives = 0;
    evaluate_rates<do_T_derivatives, rate_derivs_t>(state, rate_eval);

    // Species Jacobian elements with respect to other species

    jac_nuc(state, jac, Y, rate_eval.screened_rates);

}

#endif
