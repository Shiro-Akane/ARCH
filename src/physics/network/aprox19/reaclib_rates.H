#ifndef REACLIB_RATES_H
#define REACLIB_RATES_H

#include <amrex_bridge.H>

#include <tfactors.H>
#include <actual_network.H>
#include <rate_type.H>

using namespace Rates;
using namespace Species;

template <int do_T_derivatives>
inline
void rate_n_to_p_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // n --> p

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // wc12w
    ln_set_rate =  -6.78161;
    amrex::ignore_unused(tfactors);

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 = 0.0;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_to_n_He3_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // He4 --> n + He3

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // ka02n
    ln_set_rate =  33.0131 + -238.79 * tfactors.T9i + -1.50147 * tfactors.T913
                         + 2.5 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  238.79 * tfactors.T9i * tfactors.T9i + (1.0/3.0) * -1.50147 * tfactors.T923i
                                  + 2.5 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // ka02n
    ln_set_rate =  29.4845 + -238.79 * tfactors.T9i
                         + 1.5 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  238.79 * tfactors.T9i * tfactors.T9i
                                  + 1.5 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_O16_to_He4_C12_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // O16 --> He4 + C12

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // nac2 
    ln_set_rate =  94.3131 + -84.503 * tfactors.T9i + 58.9128 * tfactors.T913i + -148.273 * tfactors.T913
                         + 9.08324 * tfactors.T9 + -0.541041 * tfactors.T953 + 71.8554 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  84.503 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * 58.9128 * tfactors.T943i + (1.0/3.0) * -148.273 * tfactors.T923i
                                  + 9.08324 + (5.0/3.0) * -0.541041 * tfactors.T923 + 71.8554 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // nac2 
    ln_set_rate =  279.295 + -84.9515 * tfactors.T9i + 103.411 * tfactors.T913i + -420.567 * tfactors.T913
                         + 64.0874 * tfactors.T9 + -12.4624 * tfactors.T953 + 138.803 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  84.9515 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * 103.411 * tfactors.T943i + (1.0/3.0) * -420.567 * tfactors.T923i
                                  + 64.0874 + (5.0/3.0) * -12.4624 * tfactors.T923 + 138.803 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_Ne20_to_He4_O16_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Ne20 --> He4 + O16

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // co10r
    ln_set_rate =  28.6431 + -65.246 * tfactors.T9i;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  65.246 * tfactors.T9i * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // co10n
    ln_set_rate =  48.6604 + -54.8875 * tfactors.T9i + -39.7262 * tfactors.T913i + -0.210799 * tfactors.T913
                         + 0.442879 * tfactors.T9 + -0.0797753 * tfactors.T953 + 0.833333 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  54.8875 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -39.7262 * tfactors.T943i + (1.0/3.0) * -0.210799 * tfactors.T923i
                                  + 0.442879 + (5.0/3.0) * -0.0797753 * tfactors.T923 + 0.833333 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // co10r
    ln_set_rate =  34.2658 + -67.6518 * tfactors.T9i + -3.65925 * tfactors.T913
                         + 0.714224 * tfactors.T9 + -0.00107508 * tfactors.T953;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  67.6518 * tfactors.T9i * tfactors.T9i + (1.0/3.0) * -3.65925 * tfactors.T923i
                                  + 0.714224 + (5.0/3.0) * -0.00107508 * tfactors.T923;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_Mg24_to_He4_Ne20_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Mg24 --> He4 + Ne20

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // il10r
    ln_set_rate =  16.0203 + -120.895 * tfactors.T9i + 16.9229 * tfactors.T913
                         + -2.57325 * tfactors.T9 + 0.208997 * tfactors.T953;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  120.895 * tfactors.T9i * tfactors.T9i + (1.0/3.0) * 16.9229 * tfactors.T923i
                                  + -2.57325 + (5.0/3.0) * 0.208997 * tfactors.T923;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // il10r
    ln_set_rate =  26.8017 + -117.334 * tfactors.T9i;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  117.334 * tfactors.T9i * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // il10r
    ln_set_rate =  -13.8869 + -110.62 * tfactors.T9i;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  110.62 * tfactors.T9i * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // il10n
    ln_set_rate =  49.3244 + -108.114 * tfactors.T9i + -46.2525 * tfactors.T913i + 5.58901 * tfactors.T913
                         + 7.61843 * tfactors.T9 + -3.683 * tfactors.T953 + 0.833333 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  108.114 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -46.2525 * tfactors.T943i + (1.0/3.0) * 5.58901 * tfactors.T923i
                                  + 7.61843 + (5.0/3.0) * -3.683 * tfactors.T923 + 0.833333 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_Si28_to_He4_Mg24_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Si28 --> He4 + Mg24

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // st08r
    ln_set_rate =  32.9006 + -131.488 * tfactors.T9i;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  131.488 * tfactors.T9i * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // st08r
    ln_set_rate =  -25.6886 + -128.693 * tfactors.T9i + 21.3721 * tfactors.T913i + 37.7649 * tfactors.T913
                         + -4.10635 * tfactors.T9 + 0.249618 * tfactors.T953;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  128.693 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * 21.3721 * tfactors.T943i + (1.0/3.0) * 37.7649 * tfactors.T923i
                                  + -4.10635 + (5.0/3.0) * 0.249618 * tfactors.T923;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_S32_to_He4_Si28_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // S32 --> He4 + Si28

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // ths8r
    ln_set_rate =  72.813 + -80.626 * tfactors.T9i + -59.4896 * tfactors.T913i + 4.47205 * tfactors.T913
                         + -4.78989 * tfactors.T9 + 0.557201 * tfactors.T953 + 0.833333 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  80.626 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -59.4896 * tfactors.T943i + (1.0/3.0) * 4.47205 * tfactors.T923i
                                  + -4.78989 + (5.0/3.0) * 0.557201 * tfactors.T923 + 0.833333 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_Ar36_to_He4_S32_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Ar36 --> He4 + S32

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // ths8r
    ln_set_rate =  73.8164 + -77.0627 * tfactors.T9i + -65.3709 * tfactors.T913i + 5.68294 * tfactors.T913
                         + -5.00388 * tfactors.T9 + 0.571407 * tfactors.T953 + 0.833333 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  77.0627 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -65.3709 * tfactors.T943i + (1.0/3.0) * 5.68294 * tfactors.T923i
                                  + -5.00388 + (5.0/3.0) * 0.571407 * tfactors.T923 + 0.833333 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_Ca40_to_He4_Ar36_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Ca40 --> He4 + Ar36

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // ths8r
    ln_set_rate =  77.2826 + -81.6916 * tfactors.T9i + -71.0046 * tfactors.T913i + 4.0656 * tfactors.T913
                         + -5.26509 * tfactors.T9 + 0.683546 * tfactors.T953 + 0.833333 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  81.6916 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -71.0046 * tfactors.T943i + (1.0/3.0) * 4.0656 * tfactors.T923i
                                  + -5.26509 + (5.0/3.0) * 0.683546 * tfactors.T923 + 0.833333 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_Ti44_to_He4_Ca40_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Ti44 --> He4 + Ca40

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // chw0 
    ln_set_rate =  78.6991 + -59.4974 * tfactors.T9i + -76.4273 * tfactors.T913i + 3.87451 * tfactors.T913
                         + -3.61477 * tfactors.T9 + 0.367451 * tfactors.T953 + 0.833333 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  59.4974 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -76.4273 * tfactors.T943i + (1.0/3.0) * 3.87451 * tfactors.T923i
                                  + -3.61477 + (5.0/3.0) * 0.367451 * tfactors.T923 + 0.833333 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_Cr48_to_He4_Ti44_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Cr48 --> He4 + Ti44

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // ths8r
    ln_set_rate =  89.7573 + -89.3041 * tfactors.T9i + -81.667 * tfactors.T913i + -10.6333 * tfactors.T913
                         + -0.672613 * tfactors.T9 + 0.161209 * tfactors.T953 + 0.833333 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  89.3041 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -81.667 * tfactors.T943i + (1.0/3.0) * -10.6333 * tfactors.T923i
                                  + -0.672613 + (5.0/3.0) * 0.161209 * tfactors.T923 + 0.833333 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_Fe52_to_He4_Cr48_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Fe52 --> He4 + Cr48

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // ths8r
    ln_set_rate =  90.1474 + -92.109 * tfactors.T9i + -86.7459 * tfactors.T913i + -9.79373 * tfactors.T913
                         + -0.772169 * tfactors.T9 + 0.155883 * tfactors.T953 + 0.833333 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  92.109 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -86.7459 * tfactors.T943i + (1.0/3.0) * -9.79373 * tfactors.T923i
                                  + -0.772169 + (5.0/3.0) * 0.155883 * tfactors.T923 + 0.833333 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_Ni56_to_He4_Fe52_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Ni56 --> He4 + Fe52

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // ths8r
    ln_set_rate =  91.6226 + -92.801 * tfactors.T9i + -91.6819 * tfactors.T913i + -9.51885 * tfactors.T913
                         + -0.533014 * tfactors.T9 + 0.0892607 * tfactors.T953 + 0.833333 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  92.801 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -91.6819 * tfactors.T943i + (1.0/3.0) * -9.51885 * tfactors.T923i
                                  + -0.533014 + (5.0/3.0) * 0.0892607 * tfactors.T923 + 0.833333 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_C12_to_He4_He4_He4_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // C12 --> 3 He4

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // fy05r
    ln_set_rate =  22.394 + -88.5493 * tfactors.T9i + -13.49 * tfactors.T913i + 21.4259 * tfactors.T913
                         + -1.34769 * tfactors.T9 + 0.0879816 * tfactors.T953 + -10.1653 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  88.5493 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -13.49 * tfactors.T943i + (1.0/3.0) * 21.4259 * tfactors.T923i
                                  + -1.34769 + (5.0/3.0) * 0.0879816 * tfactors.T923 + -10.1653 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // fy05r
    ln_set_rate =  34.9561 + -85.4472 * tfactors.T9i + -23.57 * tfactors.T913i + 20.4886 * tfactors.T913
                         + -12.9882 * tfactors.T9 + -20.0 * tfactors.T953 + 0.83333 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  85.4472 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -23.57 * tfactors.T943i + (1.0/3.0) * 20.4886 * tfactors.T923i
                                  + -12.9882 + (5.0/3.0) * -20.0 * tfactors.T923 + 0.83333 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // fy05n
    ln_set_rate =  45.7734 + -84.4227 * tfactors.T9i + -37.06 * tfactors.T913i + 29.3493 * tfactors.T913
                         + -115.507 * tfactors.T9 + -10.0 * tfactors.T953 + 1.66667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  84.4227 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -37.06 * tfactors.T943i + (1.0/3.0) * 29.3493 * tfactors.T923i
                                  + -115.507 + (5.0/3.0) * -10.0 * tfactors.T923 + 1.66667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_n_He3_to_He4_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // He3 + n --> He4

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // ka02n
    ln_set_rate =  9.04572 + -1.50147 * tfactors.T913
                         + 1.0 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  + (1.0/3.0) * -1.50147 * tfactors.T923i
                                  + 1.0 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // ka02n
    ln_set_rate =  5.51711;
    amrex::ignore_unused(tfactors);

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 = 0.0;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_p_He3_to_He4_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // He3 + p --> He4

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // bet+w
    ln_set_rate =  -27.7611 + -4.30107e-12 * tfactors.T9i + -6.141 * tfactors.T913i + -1.93473e-09 * tfactors.T913
                         + 2.04145e-10 * tfactors.T9 + -1.80372e-11 * tfactors.T953 + -0.666667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  4.30107e-12 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -6.141 * tfactors.T943i + (1.0/3.0) * -1.93473e-09 * tfactors.T923i
                                  + 2.04145e-10 + (5.0/3.0) * -1.80372e-11 * tfactors.T923 + -0.666667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_C12_to_O16_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // C12 + He4 --> O16

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // nac2 
    ln_set_rate =  254.634 + -1.84097 * tfactors.T9i + 103.411 * tfactors.T913i + -420.567 * tfactors.T913
                         + 64.0874 * tfactors.T9 + -12.4624 * tfactors.T953 + 137.303 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  1.84097 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * 103.411 * tfactors.T943i + (1.0/3.0) * -420.567 * tfactors.T923i
                                  + 64.0874 + (5.0/3.0) * -12.4624 * tfactors.T923 + 137.303 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // nac2 
    ln_set_rate =  69.6526 + -1.39254 * tfactors.T9i + 58.9128 * tfactors.T913i + -148.273 * tfactors.T913
                         + 9.08324 * tfactors.T9 + -0.541041 * tfactors.T953 + 70.3554 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  1.39254 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * 58.9128 * tfactors.T943i + (1.0/3.0) * -148.273 * tfactors.T923i
                                  + 9.08324 + (5.0/3.0) * -0.541041 * tfactors.T923 + 70.3554 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_O16_to_Ne20_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // O16 + He4 --> Ne20

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // co10r
    ln_set_rate =  3.88571 + -10.3585 * tfactors.T9i
                         + -1.5 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  10.3585 * tfactors.T9i * tfactors.T9i
                                  + -1.5 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // co10n
    ln_set_rate =  23.903 + -39.7262 * tfactors.T913i + -0.210799 * tfactors.T913
                         + 0.442879 * tfactors.T9 + -0.0797753 * tfactors.T953 + -0.666667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  + -(1.0/3.0) * -39.7262 * tfactors.T943i + (1.0/3.0) * -0.210799 * tfactors.T923i
                                  + 0.442879 + (5.0/3.0) * -0.0797753 * tfactors.T923 + -0.666667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // co10r
    ln_set_rate =  9.50848 + -12.7643 * tfactors.T9i + -3.65925 * tfactors.T913
                         + 0.714224 * tfactors.T9 + -0.00107508 * tfactors.T953 + -1.5 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  12.7643 * tfactors.T9i * tfactors.T9i + (1.0/3.0) * -3.65925 * tfactors.T923i
                                  + 0.714224 + (5.0/3.0) * -0.00107508 * tfactors.T923 + -1.5 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_Ne20_to_Mg24_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Ne20 + He4 --> Mg24

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // il10r
    ln_set_rate =  -8.79827 + -12.7809 * tfactors.T9i + 16.9229 * tfactors.T913
                         + -2.57325 * tfactors.T9 + 0.208997 * tfactors.T953 + -1.5 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  12.7809 * tfactors.T9i * tfactors.T9i + (1.0/3.0) * 16.9229 * tfactors.T923i
                                  + -2.57325 + (5.0/3.0) * 0.208997 * tfactors.T923 + -1.5 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // il10r
    ln_set_rate =  1.98307 + -9.22026 * tfactors.T9i
                         + -1.5 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  9.22026 * tfactors.T9i * tfactors.T9i
                                  + -1.5 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // il10r
    ln_set_rate =  -38.7055 + -2.50605 * tfactors.T9i
                         + -1.5 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  2.50605 * tfactors.T9i * tfactors.T9i
                                  + -1.5 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // il10n
    ln_set_rate =  24.5058 + -46.2525 * tfactors.T913i + 5.58901 * tfactors.T913
                         + 7.61843 * tfactors.T9 + -3.683 * tfactors.T953 + -0.666667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  + -(1.0/3.0) * -46.2525 * tfactors.T943i + (1.0/3.0) * 5.58901 * tfactors.T923i
                                  + 7.61843 + (5.0/3.0) * -3.683 * tfactors.T923 + -0.666667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_Mg24_to_Si28_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Mg24 + He4 --> Si28

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // st08r
    ln_set_rate =  8.03977 + -15.629 * tfactors.T9i
                         + -1.5 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  15.629 * tfactors.T9i * tfactors.T9i
                                  + -1.5 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // st08r
    ln_set_rate =  -50.5494 + -12.8332 * tfactors.T9i + 21.3721 * tfactors.T913i + 37.7649 * tfactors.T913
                         + -4.10635 * tfactors.T9 + 0.249618 * tfactors.T953 + -1.5 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  12.8332 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * 21.3721 * tfactors.T943i + (1.0/3.0) * 37.7649 * tfactors.T923i
                                  + -4.10635 + (5.0/3.0) * 0.249618 * tfactors.T923 + -1.5 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_Si28_to_S32_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Si28 + He4 --> S32

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // ths8r
    ln_set_rate =  47.9212 + -59.4896 * tfactors.T913i + 4.47205 * tfactors.T913
                         + -4.78989 * tfactors.T9 + 0.557201 * tfactors.T953 + -0.666667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  + -(1.0/3.0) * -59.4896 * tfactors.T943i + (1.0/3.0) * 4.47205 * tfactors.T923i
                                  + -4.78989 + (5.0/3.0) * 0.557201 * tfactors.T923 + -0.666667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_S32_to_Ar36_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // S32 + He4 --> Ar36

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // ths8r
    ln_set_rate =  48.901 + -65.3709 * tfactors.T913i + 5.68294 * tfactors.T913
                         + -5.00388 * tfactors.T9 + 0.571407 * tfactors.T953 + -0.666667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  + -(1.0/3.0) * -65.3709 * tfactors.T943i + (1.0/3.0) * 5.68294 * tfactors.T923i
                                  + -5.00388 + (5.0/3.0) * 0.571407 * tfactors.T923 + -0.666667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_Ar36_to_Ca40_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Ar36 + He4 --> Ca40

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // ths8r
    ln_set_rate =  52.3486 + -71.0046 * tfactors.T913i + 4.0656 * tfactors.T913
                         + -5.26509 * tfactors.T9 + 0.683546 * tfactors.T953 + -0.666667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  + -(1.0/3.0) * -71.0046 * tfactors.T943i + (1.0/3.0) * 4.0656 * tfactors.T923i
                                  + -5.26509 + (5.0/3.0) * 0.683546 * tfactors.T923 + -0.666667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_Ca40_to_Ti44_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Ca40 + He4 --> Ti44

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // chw0 
    ln_set_rate =  53.75 + -76.4273 * tfactors.T913i + 3.87451 * tfactors.T913
                         + -3.61477 * tfactors.T9 + 0.367451 * tfactors.T953 + -0.666667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  + -(1.0/3.0) * -76.4273 * tfactors.T943i + (1.0/3.0) * 3.87451 * tfactors.T923i
                                  + -3.61477 + (5.0/3.0) * 0.367451 * tfactors.T923 + -0.666667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_Ti44_to_Cr48_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Ti44 + He4 --> Cr48

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // ths8r
    ln_set_rate =  64.7958 + -81.667 * tfactors.T913i + -10.6333 * tfactors.T913
                         + -0.672613 * tfactors.T9 + 0.161209 * tfactors.T953 + -0.666667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  + -(1.0/3.0) * -81.667 * tfactors.T943i + (1.0/3.0) * -10.6333 * tfactors.T923i
                                  + -0.672613 + (5.0/3.0) * 0.161209 * tfactors.T923 + -0.666667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_Cr48_to_Fe52_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Cr48 + He4 --> Fe52

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // ths8r
    ln_set_rate =  65.1754 + -86.7459 * tfactors.T913i + -9.79373 * tfactors.T913
                         + -0.772169 * tfactors.T9 + 0.155883 * tfactors.T953 + -0.666667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  + -(1.0/3.0) * -86.7459 * tfactors.T943i + (1.0/3.0) * -9.79373 * tfactors.T923i
                                  + -0.772169 + (5.0/3.0) * 0.155883 * tfactors.T923 + -0.666667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_Fe52_to_Ni56_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Fe52 + He4 --> Ni56

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // ths8r
    ln_set_rate =  66.6417 + -91.6819 * tfactors.T913i + -9.51885 * tfactors.T913
                         + -0.533014 * tfactors.T9 + 0.0892607 * tfactors.T953 + -0.666667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  + -(1.0/3.0) * -91.6819 * tfactors.T943i + (1.0/3.0) * -9.51885 * tfactors.T923i
                                  + -0.533014 + (5.0/3.0) * 0.0892607 * tfactors.T923 + -0.666667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_C12_C12_to_He4_Ne20_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // C12 + C12 --> He4 + Ne20

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // cf88r
    ln_set_rate =  61.2863 + -84.165 * tfactors.T913i + -1.56627 * tfactors.T913
                         + -0.0736084 * tfactors.T9 + -0.072797 * tfactors.T953 + -0.666667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  + -(1.0/3.0) * -84.165 * tfactors.T943i + (1.0/3.0) * -1.56627 * tfactors.T923i
                                  + -0.0736084 + (5.0/3.0) * -0.072797 * tfactors.T923 + -0.666667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_C12_O16_to_He4_Mg24_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // O16 + C12 --> He4 + Mg24

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // cf88r
    ln_set_rate =  48.5341 + 0.37204 * tfactors.T9i + -133.413 * tfactors.T913i + 50.1572 * tfactors.T913
                         + -3.15987 * tfactors.T9 + 0.0178251 * tfactors.T953 + -23.7027 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  -0.37204 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -133.413 * tfactors.T943i + (1.0/3.0) * 50.1572 * tfactors.T923i
                                  + -3.15987 + (5.0/3.0) * 0.0178251 * tfactors.T923 + -23.7027 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_O16_O16_to_He4_Si28_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // O16 + O16 --> He4 + Si28

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // cf88r
    ln_set_rate =  97.2435 + -0.268514 * tfactors.T9i + -119.324 * tfactors.T913i + -32.2497 * tfactors.T913
                         + 1.46214 * tfactors.T9 + -0.200893 * tfactors.T953 + 13.2148 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  0.268514 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -119.324 * tfactors.T943i + (1.0/3.0) * -32.2497 * tfactors.T923i
                                  + 1.46214 + (5.0/3.0) * -0.200893 * tfactors.T923 + 13.2148 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_Ne20_to_C12_C12_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Ne20 + He4 --> C12 + C12

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // cf88r
    ln_set_rate =  61.4748 + -53.6267 * tfactors.T9i + -84.165 * tfactors.T913i + -1.56627 * tfactors.T913
                         + -0.0736084 * tfactors.T9 + -0.072797 * tfactors.T953 + -0.666667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  53.6267 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -84.165 * tfactors.T943i + (1.0/3.0) * -1.56627 * tfactors.T923i
                                  + -0.0736084 + (5.0/3.0) * -0.072797 * tfactors.T923 + -0.666667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_C12_Ne20_to_He4_Si28_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Ne20 + C12 --> He4 + Si28

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // rolfr
    ln_set_rate =  -308.905 + -47.2175 * tfactors.T9i + 514.197 * tfactors.T913i + -200.896 * tfactors.T913
                         + -6.42713 * tfactors.T9 + 0.758256 * tfactors.T953 + 236.359 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  47.2175 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * 514.197 * tfactors.T943i + (1.0/3.0) * -200.896 * tfactors.T923i
                                  + -6.42713 + (5.0/3.0) * 0.758256 * tfactors.T923 + 236.359 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_Mg24_to_C12_O16_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Mg24 + He4 --> C12 + O16

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // cf88r
    ln_set_rate =  49.5738 + -78.202 * tfactors.T9i + -133.413 * tfactors.T913i + 50.1572 * tfactors.T913
                         + -3.15987 * tfactors.T9 + 0.0178251 * tfactors.T953 + -23.7027 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  78.202 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -133.413 * tfactors.T943i + (1.0/3.0) * 50.1572 * tfactors.T923i
                                  + -3.15987 + (5.0/3.0) * 0.0178251 * tfactors.T923 + -23.7027 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_Si28_to_C12_Ne20_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Si28 + He4 --> C12 + Ne20

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // rolfr
    ln_set_rate =  -307.762 + -186.722 * tfactors.T9i + 514.197 * tfactors.T913i + -200.896 * tfactors.T913
                         + -6.42713 * tfactors.T9 + 0.758256 * tfactors.T953 + 236.359 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  186.722 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * 514.197 * tfactors.T943i + (1.0/3.0) * -200.896 * tfactors.T923i
                                  + -6.42713 + (5.0/3.0) * 0.758256 * tfactors.T923 + 236.359 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_Si28_to_O16_O16_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // Si28 + He4 --> O16 + O16

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // cf88r
    ln_set_rate =  97.7904 + -111.595 * tfactors.T9i + -119.324 * tfactors.T913i + -32.2497 * tfactors.T913
                         + 1.46214 * tfactors.T9 + -0.200893 * tfactors.T953 + 13.2148 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  111.595 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -119.324 * tfactors.T943i + (1.0/3.0) * -32.2497 * tfactors.T923i
                                  + 1.46214 + (5.0/3.0) * -0.200893 * tfactors.T923 + 13.2148 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He3_He3_to_p_p_He4_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // He3 + He3 --> p + p + He4

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // nacrn
    ln_set_rate =  24.7788 + -12.277 * tfactors.T913i + -0.103699 * tfactors.T913
                         + -0.0649967 * tfactors.T9 + 0.0168191 * tfactors.T953 + -0.666667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  + -(1.0/3.0) * -12.277 * tfactors.T943i + (1.0/3.0) * -0.103699 * tfactors.T923i
                                  + -0.0649967 + (5.0/3.0) * 0.0168191 * tfactors.T923 + -0.666667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_He4_He4_He4_to_C12_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // 3 He4 --> C12

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // fy05r
    ln_set_rate =  -11.7884 + -1.02446 * tfactors.T9i + -23.57 * tfactors.T913i + 20.4886 * tfactors.T913
                         + -12.9882 * tfactors.T9 + -20.0 * tfactors.T953 + -2.16667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  1.02446 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -23.57 * tfactors.T943i + (1.0/3.0) * 20.4886 * tfactors.T923i
                                  + -12.9882 + (5.0/3.0) * -20.0 * tfactors.T923 + -2.16667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // fy05n
    ln_set_rate =  -0.971052 + -37.06 * tfactors.T913i + 29.3493 * tfactors.T913
                         + -115.507 * tfactors.T9 + -10.0 * tfactors.T953 + -1.33333 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  + -(1.0/3.0) * -37.06 * tfactors.T943i + (1.0/3.0) * 29.3493 * tfactors.T923i
                                  + -115.507 + (5.0/3.0) * -10.0 * tfactors.T923 + -1.33333 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

    // fy05r
    ln_set_rate =  -24.3505 + -4.12656 * tfactors.T9i + -13.49 * tfactors.T913i + 21.4259 * tfactors.T913
                         + -1.34769 * tfactors.T9 + 0.0879816 * tfactors.T953 + -13.1653 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  4.12656 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -13.49 * tfactors.T943i + (1.0/3.0) * 21.4259 * tfactors.T923i
                                  + -1.34769 + (5.0/3.0) * 0.0879816 * tfactors.T923 + -13.1653 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}

template <int do_T_derivatives>
inline
void rate_p_p_He4_to_He3_He3_reaclib(const tf_t& tfactors, const Real log_scor, const Real dlog_scor_dT, Real& rate, Real& drate_dT) {

    // p + p + He4 --> He3 + He3

    rate = 0.0;
    drate_dT = 0.0;

    Real ln_set_rate{0.0};
    Real dln_set_rate_dT9{0.0};
    Real set_rate{0.0};

    // nacrn
    ln_set_rate =  2.98257 + -149.222 * tfactors.T9i + -12.277 * tfactors.T913i + -0.103699 * tfactors.T913
                         + -0.0649967 * tfactors.T9 + 0.0168191 * tfactors.T953 + -2.16667 * tfactors.lnT9;

    ln_set_rate += log_scor;

    if constexpr (do_T_derivatives) {
        dln_set_rate_dT9 =  149.222 * tfactors.T9i * tfactors.T9i + -(1.0/3.0) * -12.277 * tfactors.T943i + (1.0/3.0) * -0.103699 * tfactors.T923i
                                  + -0.0649967 + (5.0/3.0) * 0.0168191 * tfactors.T923 + -2.16667 * tfactors.T9i;

        dln_set_rate_dT9 += dlog_scor_dT * 1.0e9_rt;
    }

    // avoid underflows by zeroing rates in [0.0, 1.e-100]
    ln_set_rate = std::max(ln_set_rate, -230.0);
    set_rate = std::exp(ln_set_rate);
    rate += set_rate;
    if constexpr (do_T_derivatives) {
        drate_dT += set_rate * dln_set_rate_dT9 * 1.0e-9;
    }

}


template <int do_T_derivatives, typename T>
inline
void
fill_reaclib_rates(const tf_t& tfactors, T& rate_eval)
{

    Real rate;
    Real drate_dT;

    {
        // n_to_p_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
        rate_n_to_p_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_n_to_p_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_n_to_p_reaclib) = drate_dT;
        }
    }

    {
        // He4_to_n_He3_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
        rate_He4_to_n_He3_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_to_n_He3_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_to_n_He3_reaclib) = drate_dT;
        }
    }

    {
        // O16_to_He4_C12_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
        rate_O16_to_He4_C12_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_O16_to_He4_C12_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_O16_to_He4_C12_reaclib) = drate_dT;
        }
    }

    {
        // Ne20_to_He4_O16_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
        rate_Ne20_to_He4_O16_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_Ne20_to_He4_O16_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_Ne20_to_He4_O16_reaclib) = drate_dT;
        }
    }

    {
        // Mg24_to_He4_Ne20_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
        rate_Mg24_to_He4_Ne20_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_Mg24_to_He4_Ne20_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_Mg24_to_He4_Ne20_reaclib) = drate_dT;
        }
    }

    {
        // Si28_to_He4_Mg24_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
        rate_Si28_to_He4_Mg24_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_Si28_to_He4_Mg24_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_Si28_to_He4_Mg24_reaclib) = drate_dT;
        }
    }

    {
        // S32_to_He4_Si28_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
        rate_S32_to_He4_Si28_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_S32_to_He4_Si28_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_S32_to_He4_Si28_reaclib) = drate_dT;
        }
    }

    {
        // Ar36_to_He4_S32_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
        rate_Ar36_to_He4_S32_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_Ar36_to_He4_S32_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_Ar36_to_He4_S32_reaclib) = drate_dT;
        }
    }

    {
        // Ca40_to_He4_Ar36_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
        rate_Ca40_to_He4_Ar36_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_Ca40_to_He4_Ar36_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_Ca40_to_He4_Ar36_reaclib) = drate_dT;
        }
    }

    {
        // Ti44_to_He4_Ca40_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
        rate_Ti44_to_He4_Ca40_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_Ti44_to_He4_Ca40_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_Ti44_to_He4_Ca40_reaclib) = drate_dT;
        }
    }

    {
        // Cr48_to_He4_Ti44_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
        rate_Cr48_to_He4_Ti44_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_Cr48_to_He4_Ti44_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_Cr48_to_He4_Ti44_reaclib) = drate_dT;
        }
    }

    {
        // Fe52_to_He4_Cr48_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
        rate_Fe52_to_He4_Cr48_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_Fe52_to_He4_Cr48_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_Fe52_to_He4_Cr48_reaclib) = drate_dT;
        }
    }

    {
        // Ni56_to_He4_Fe52_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
        rate_Ni56_to_He4_Fe52_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_Ni56_to_He4_Fe52_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_Ni56_to_He4_Fe52_reaclib) = drate_dT;
        }
    }

    {
        // C12_to_He4_He4_He4_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
        rate_C12_to_He4_He4_He4_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_C12_to_He4_He4_He4_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_C12_to_He4_He4_He4_reaclib) = drate_dT;
        }
    }

    {
        // n_He3_to_He4_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
        rate_n_He3_to_He4_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_n_He3_to_He4_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_n_He3_to_He4_reaclib) = drate_dT;
        }
    }

    {
        // p_He3_to_He4_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_p_He3);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_p_He3);
        }
#endif
        rate_p_He3_to_He4_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_p_He3_to_He4_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_p_He3_to_He4_reaclib) = drate_dT;
        }
    }

    {
        // He4_C12_to_O16_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_C12);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_C12);
        }
#endif
        rate_He4_C12_to_O16_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_C12_to_O16_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_C12_to_O16_reaclib) = drate_dT;
        }
    }

    {
        // He4_O16_to_Ne20_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_O16);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_O16);
        }
#endif
        rate_He4_O16_to_Ne20_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_O16_to_Ne20_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_O16_to_Ne20_reaclib) = drate_dT;
        }
    }

    {
        // He4_Ne20_to_Mg24_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_Ne20);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_Ne20);
        }
#endif
        rate_He4_Ne20_to_Mg24_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_Ne20_to_Mg24_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_Ne20_to_Mg24_reaclib) = drate_dT;
        }
    }

    {
        // He4_Mg24_to_Si28_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_Mg24);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_Mg24);
        }
#endif
        rate_He4_Mg24_to_Si28_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_Mg24_to_Si28_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_Mg24_to_Si28_reaclib) = drate_dT;
        }
    }

    {
        // He4_Si28_to_S32_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_Si28);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_Si28);
        }
#endif
        rate_He4_Si28_to_S32_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_Si28_to_S32_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_Si28_to_S32_reaclib) = drate_dT;
        }
    }

    {
        // He4_S32_to_Ar36_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_S32);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_S32);
        }
#endif
        rate_He4_S32_to_Ar36_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_S32_to_Ar36_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_S32_to_Ar36_reaclib) = drate_dT;
        }
    }

    {
        // He4_Ar36_to_Ca40_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_Ar36);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_Ar36);
        }
#endif
        rate_He4_Ar36_to_Ca40_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_Ar36_to_Ca40_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_Ar36_to_Ca40_reaclib) = drate_dT;
        }
    }

    {
        // He4_Ca40_to_Ti44_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_Ca40);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_Ca40);
        }
#endif
        rate_He4_Ca40_to_Ti44_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_Ca40_to_Ti44_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_Ca40_to_Ti44_reaclib) = drate_dT;
        }
    }

    {
        // He4_Ti44_to_Cr48_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_Ti44);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_Ti44);
        }
#endif
        rate_He4_Ti44_to_Cr48_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_Ti44_to_Cr48_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_Ti44_to_Cr48_reaclib) = drate_dT;
        }
    }

    {
        // He4_Cr48_to_Fe52_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_Cr48);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_Cr48);
        }
#endif
        rate_He4_Cr48_to_Fe52_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_Cr48_to_Fe52_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_Cr48_to_Fe52_reaclib) = drate_dT;
        }
    }

    {
        // He4_Fe52_to_Ni56_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_Fe52);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_Fe52);
        }
#endif
        rate_He4_Fe52_to_Ni56_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_Fe52_to_Ni56_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_Fe52_to_Ni56_reaclib) = drate_dT;
        }
    }

    {
        // C12_C12_to_He4_Ne20_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_C12_C12);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_C12_C12);
        }
#endif
        rate_C12_C12_to_He4_Ne20_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_C12_C12_to_He4_Ne20_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_C12_C12_to_He4_Ne20_reaclib) = drate_dT;
        }
    }

    {
        // C12_O16_to_He4_Mg24_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_C12_O16);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_C12_O16);
        }
#endif
        rate_C12_O16_to_He4_Mg24_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_C12_O16_to_He4_Mg24_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_C12_O16_to_He4_Mg24_reaclib) = drate_dT;
        }
    }

    {
        // O16_O16_to_He4_Si28_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_O16_O16);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_O16_O16);
        }
#endif
        rate_O16_O16_to_He4_Si28_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_O16_O16_to_He4_Si28_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_O16_O16_to_He4_Si28_reaclib) = drate_dT;
        }
    }

    {
        // He4_Ne20_to_C12_C12_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_Ne20);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_Ne20);
        }
#endif
        rate_He4_Ne20_to_C12_C12_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_Ne20_to_C12_C12_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_Ne20_to_C12_C12_reaclib) = drate_dT;
        }
    }

    {
        // C12_Ne20_to_He4_Si28_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_C12_Ne20);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_C12_Ne20);
        }
#endif
        rate_C12_Ne20_to_He4_Si28_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_C12_Ne20_to_He4_Si28_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_C12_Ne20_to_He4_Si28_reaclib) = drate_dT;
        }
    }

    {
        // He4_Mg24_to_C12_O16_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_Mg24);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_Mg24);
        }
#endif
        rate_He4_Mg24_to_C12_O16_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_Mg24_to_C12_O16_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_Mg24_to_C12_O16_reaclib) = drate_dT;
        }
    }

    {
        // He4_Si28_to_C12_Ne20_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_Si28);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_Si28);
        }
#endif
        rate_He4_Si28_to_C12_Ne20_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_Si28_to_C12_Ne20_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_Si28_to_C12_Ne20_reaclib) = drate_dT;
        }
    }

    {
        // He4_Si28_to_O16_O16_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_Si28);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_Si28);
        }
#endif
        rate_He4_Si28_to_O16_O16_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_Si28_to_O16_O16_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_Si28_to_O16_O16_reaclib) = drate_dT;
        }
    }

    {
        // He3_He3_to_p_p_He4_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He3_He3);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He3_He3);
        }
#endif
        rate_He3_He3_to_p_p_He4_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He3_He3_to_p_p_He4_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He3_He3_to_p_p_He4_reaclib) = drate_dT;
        }
    }

    {
        // He4_He4_He4_to_C12_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_He4_He4) + rate_eval.log_screen(k_He4_Be8);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_He4_He4) + rate_eval.dlog_screen_dT(k_He4_Be8);
        }
#endif
        rate_He4_He4_He4_to_C12_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_He4_He4_He4_to_C12_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_He4_He4_He4_to_C12_reaclib) = drate_dT;
        }
    }

    {
        // p_p_He4_to_He3_He3_reaclib

        Real log_scor {0.0_rt};
        Real dlog_scor_dT {0.0_rt};
#ifdef SCREENING
        log_scor = rate_eval.log_screen(k_p_p) + rate_eval.log_screen(k_He2_He4);
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            dlog_scor_dT = rate_eval.dlog_screen_dT(k_p_p) + rate_eval.dlog_screen_dT(k_He2_He4);
        }
#endif
        rate_p_p_He4_to_He3_He3_reaclib<do_T_derivatives>(tfactors, log_scor, dlog_scor_dT, rate, drate_dT);
        rate_eval.screened_rates(k_p_p_He4_to_He3_He3_reaclib) = rate;
        if constexpr (std::is_same_v<T, rate_derivs_t>) {
            rate_eval.dscreened_rates_dT(k_p_p_He4_to_He3_He3_reaclib) = drate_dT;
        }
    }


}

#endif
