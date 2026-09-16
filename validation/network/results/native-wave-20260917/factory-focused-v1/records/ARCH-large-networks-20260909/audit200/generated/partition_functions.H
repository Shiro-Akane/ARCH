#ifndef ARCH_GENERATED_AUDIT200_PARTITION_FUNCTIONS_H
#define ARCH_GENERATED_AUDIT200_PARTITION_FUNCTIONS_H

#include <cassert>
#include <numbers>

#include "amrex_bridge.H"

#include "tfactors.H"
#include "fundamental_constants.H"
#include "network_properties.H"
#include "interp_tools.H"

using namespace literals;
using namespace Species;

namespace part_fun {


    // interpolation routine

    template <typename T>
    ARCH_HOST_DEVICE inline
    void interpolate_pf(const Real t9, int idx,
                        const T& temp_array, const T& pf_array,
                        Real& logpf, Real& dlogpf_dT9) {

        // if the index is -1, we are either outside of the bounds or
        // we never computed it.  try recomputing here
        if (idx == -1) {
            idx = interp_net::find_index(t9, temp_array);
        }

        // if the index is still -1, then we are out-of-bounds, so
        // just skip the interpolation

        if (idx != -1) {

            // we have idx such that:
            // temp_array[idx] <= t9 < temp_array[idx+1]
            assert(t9 >= temp_array(idx) && t9 < temp_array(idx+1));

            // find the derivative: dlog(pf)/dT9 -- this is
            // (log(pf_{i+1}) - log(pf_i)) / (T_{i+1} - T_i)

            dlogpf_dT9 = (pf_array(idx+1) - pf_array(idx)) /
                           (temp_array(idx+1) - temp_array(idx));

            // find log(PF)

            logpf = pf_array(idx) + dlogpf_dT9 * (t9 - temp_array(idx));

        } else {

            // T < the smallest T or >= the largest T in the partition function table
            logpf = 0.0;
            dlogpf_dT9 = 0.0;

        }

    }

    struct pf_cache_t {
        // Note: we are using C-ordering here
        Array2D<Real, 1, NumSpecTotal, 1, 2> data;
        ARCH_HOST_DEVICE inline
        pf_cache_t() {
            // The entries will be default-initialized to -900,
            // log(partition_function) should never be -900.
            for (int i = 1; i <= NumSpecTotal; ++i) {
                    data(i, 1) = -900.0_rt;
                    data(i, 2) = -900.0_rt;
            }
        }
    };

}

// get the partition function for nucleus inuc.  Here pf_cache
// should already contain the index into the temp / pf arrays

ARCH_HOST_DEVICE inline
void get_partition_function(const int inuc,
                            [[maybe_unused]] const Real T9,
                            const part_fun::pf_cache_t& pf_cache,
                            Real& logpf, Real& dlogpf_dT9) {

    logpf = 0.0_rt;
    dlogpf_dT9 = 0.0_rt;

    // inuc is the 1-based index for the species

    switch (inuc) {


    default:

        logpf = 0.0_rt;
        dlogpf_dT9 = 0.0_rt;

    }

}


// a version of get_partition_function where we have not cached the
// index into the arrays. Uses tfactors instead of T9 as argument,
// and returns pf and dpf_dT instead of logpf and dlogpf_dT9

ARCH_HOST_DEVICE inline
void get_partition_function(const int inuc,
                            [[maybe_unused]] const tf_t& tfactors,
                            Real& pf, Real& dpf_dT) {

    // create a dummy cache.  This will initialize the indices
    // to -1 and we will force a search for the correct index
    // when we do the interpolation.

    part_fun::pf_cache_t pf_cache;

    Real logpf, dlogpf_dT9;
    get_partition_function(inuc, tfactors.T9, pf_cache, logpf, dlogpf_dT9);

    // Convert logpf to pf and dlogpf_dT9 to dpf_dT
    pf = std::exp(logpf);
    dpf_dT = pf * dlogpf_dT9 * 1.e-9_rt;
}


ARCH_HOST_DEVICE inline
void get_partition_function_cached(const int inuc, const Real T9,
                                   part_fun::pf_cache_t& pf_cache,
                                   Real& logpf, Real& dlogpf_dT9) {
    if (pf_cache.data(inuc, 1) != -900.0_rt) {
        // present in cache
        amrex::ignore_unused(T9);
        logpf = pf_cache.data(inuc, 1);
        dlogpf_dT9 = pf_cache.data(inuc, 2);
    } else {
        get_partition_function(inuc, T9, pf_cache, logpf, dlogpf_dT9);
        pf_cache.data(inuc, 1) = logpf;
        pf_cache.data(inuc, 2) = dlogpf_dT9;
    }
}


template <int spec>
ARCH_HOST_DEVICE inline
constexpr Real get_spin_state() {

    static_assert(spec >= 1 && spec <= NumSpec);

    // Set the spin states (2J + 1) of the element

    if constexpr (
        spec == He4 || spec == C12 || spec == C14 ||
        spec == O14 || spec == O16 || spec == O18 ||
        spec == O20 || spec == F16 || spec == Ne18 ||
        spec == Ne20 || spec == Ne22 || spec == Ne24 ||
        spec == Mg22 || spec == Mg24 || spec == Mg26 ||
        spec == Mg28 || spec == Si28 || spec == Si30 ||
        spec == Si32 || spec == S32 || spec == S34 ||
        spec == S36 || spec == Cl34 || spec == Ar36 ||
        spec == Ar38 || spec == Ar40 || spec == Ar42 ||
        spec == Ca40 || spec == Ca42 || spec == Ca44 ||
        spec == Ca46 || spec == Ti46 || spec == Ti48 ||
        spec == Ti50 || spec == Cr50 || spec == Cr52 ||
        spec == Cr54 || spec == Cr56 || spec == Fe54 ||
        spec == Fe56 || spec == Fe58 || spec == Fe60 ||
        spec == Ni60 || spec == Ni62 || spec == Ni64 ||
        spec == Zn64 || spec == Zn66 || spec == Zn68 ||
        spec == Zn70 || spec == Ge70 || spec == Ge72 ||
        spec == Ge74 || spec == C10
                  )
    {
        return 1.0_rt;
    }
    else if constexpr (
        spec == N || spec == H1 || spec == C13 ||
        spec == C15 || spec == N11 || spec == N13 ||
        spec == N15 || spec == N17 || spec == O15 ||
        spec == F19 || spec == Ne19 || spec == Mg27 ||
        spec == Si29 || spec == P29 || spec == P31 ||
        spec == P33 || spec == P35 || spec == S31 ||
        spec == Fe57 || spec == Ni63 || spec == Zn69 ||
        spec == Ga73 || spec == Ge71 || spec == Ge75 ||
        spec == Se77
                       )
    {
        return 2.0_rt;
    }
    else if constexpr (
        spec == N12 || spec == N14 || spec == F18 ||
        spec == P30 || spec == P32 || spec == P34 ||
        spec == Mn58 || spec == Cu62 || spec == Cu64 ||
        spec == Cu66 || spec == Cu68 || spec == Ga68 ||
        spec == Ga70
                       )
    {
        return 3.0_rt;
    }
    else if constexpr (
        spec == C9 || spec == C11 || spec == Ne21 ||
        spec == Na21 || spec == Na23 || spec == Mg23 ||
        spec == Si31 || spec == Si33 || spec == S33 ||
        spec == S35 || spec == Cl33 || spec == Cl35 ||
        spec == Cl37 || spec == Cl39 || spec == Ar37 ||
        spec == K39 || spec == K41 || spec == K43 ||
        spec == Ti51 || spec == V47 || spec == Cr53 ||
        spec == Cr55 || spec == Fe55 || spec == Fe59 ||
        spec == Ni59 || spec == Ni61 || spec == Cu63 ||
        spec == Cu65 || spec == Cu67 || spec == Ga67 ||
        spec == Ga69 || spec == Ga71 || spec == As73 ||
        spec == As75 || spec == As77
                       )
    {
        return 4.0_rt;
    }
    else if constexpr (
        spec == N16 || spec == F20 || spec == Na20 ||
        spec == Cl36 || spec == Cl38 || spec == K42 ||
        spec == K44 || spec == Sc44 || spec == Co58 ||
        spec == Co62 || spec == As72 || spec == As74 ||
        spec == As76 || spec == As78
                       )
    {
        return 5.0_rt;
    }
    else if constexpr (
        spec == O17 || spec == O19 || spec == F17 ||
        spec == F21 || spec == Ne23 || spec == Na25 ||
        spec == Mg25 || spec == Al25 || spec == Al27 ||
        spec == Al29 || spec == Si27 || spec == Ti47 ||
        spec == Mn55 || spec == Mn57 || spec == Ni65 ||
        spec == Zn65 || spec == Zn67 || spec == Ge69
                       )
    {
        return 6.0_rt;
    }
    else if constexpr (
        spec == Na22 || spec == Na26 || spec == Al28 ||
        spec == Al30 || spec == K38 || spec == V52 ||
        spec == Mn54 || spec == Mn56 || spec == Ga72
                       )
    {
        return 7.0_rt;
    }
    else if constexpr (
        spec == S37 || spec == Ar39 || spec == Ar41 ||
        spec == Ca41 || spec == Ca43 || spec == Ca45 ||
        spec == Sc43 || spec == Sc45 || spec == Sc47 ||
        spec == Sc49 || spec == Ti45 || spec == Ti49 ||
        spec == V49 || spec == V51 || spec == V53 ||
        spec == Cr51 || spec == Mn53 || spec == Co57 ||
        spec == Co59 || spec == Co61 || spec == Co63
                       )
    {
        return 8.0_rt;
    }
    else if constexpr (
        spec == F22 || spec == Na24 || spec == Al24 ||
        spec == K40 || spec == Sc46 || spec == V48
                       )
    {
        return 9.0_rt;
    }
    else if constexpr (
        spec == Ge73
                       )
    {
        return 10.0_rt;
    }
    else if constexpr (
        spec == Al26 || spec == Co60
                       )
    {
        return 11.0_rt;
    }
    else if constexpr (
        spec == Sc48 || spec == V50 || spec == Mn52
                       )
    {
        return 13.0_rt;
    }

    // Return -1 if we don't recognize the species.
    return -1.0_rt;
}


// Legacy (non-templated) interface

ARCH_HOST_DEVICE inline
Real get_spin_state (int spec) {

    Real s = -1.0_rt;

    // Set the spin states (2J + 1) of the element

    amrex::constexpr_for<1, NumSpec+1>([&] (auto n) {
        if (n == spec) {
            s = get_spin_state<n>();
        }
    });

    return s;
}


#endif
