#pragma once

#include <array>
#include <cstddef>

#include "RatePair.h"
#include "ScreeningTimmes.h"
#include "TfactorsData.h"

namespace timmes {

template <typename Array>
inline void store_pair(Array& rates, int forward, int reverse, const RatePair& pair)
{
    rates[forward] = pair.forward;
    rates[reverse] = pair.reverse;
}

template <typename Ids, typename Library, typename Array>
inline void fill_heavy_rates(Array& r, double temperature, double density,
                             const TfactorsData& tf)
{
    store_pair(r, Ids::ir3a, Ids::irg3a, Library::rate_tripalf(temperature, density, tf));
    store_pair(r, Ids::ircag, Ids::iroga, Library::rate_c12ag(temperature, density, tf));
    r[Ids::ir1212] = Library::rate_c12c12(temperature, density, tf).forward;
    r[Ids::ir1216] = Library::rate_c12o16(temperature, density, tf).forward;
    r[Ids::ir1616] = Library::rate_o16o16(temperature, density, tf).forward;
    store_pair(r, Ids::iroag, Ids::irnega, Library::rate_o16ag(temperature, density, tf));
    store_pair(r, Ids::irneag, Ids::irmgga, Library::rate_ne20ag(temperature, density, tf));
    store_pair(r, Ids::irmgag, Ids::irsiga, Library::rate_mg24ag(temperature, density, tf));
    store_pair(r, Ids::irmgap, Ids::iralpa, Library::rate_mg24ap(temperature, density, tf));
    store_pair(r, Ids::iralpg, Ids::irsigp, Library::rate_al27pg(temperature, density, tf));
    store_pair(r, Ids::irsiag, Ids::irsga, Library::rate_si28ag(temperature, density, tf));
    store_pair(r, Ids::irsiap, Ids::irppa, Library::rate_si28ap(temperature, density, tf));
    store_pair(r, Ids::irppg, Ids::irsgp, Library::rate_p31pg(temperature, density, tf));
    store_pair(r, Ids::irsag, Ids::irarga, Library::rate_s32ag(temperature, density, tf));
    store_pair(r, Ids::irsap, Ids::irclpa, Library::rate_s32ap(temperature, density, tf));
    store_pair(r, Ids::irclpg, Ids::irargp, Library::rate_cl35pg(temperature, density, tf));
    store_pair(r, Ids::irarag, Ids::ircaga, Library::rate_ar36ag(temperature, density, tf));
    store_pair(r, Ids::irarap, Ids::irkpa, Library::rate_ar36ap(temperature, density, tf));
    store_pair(r, Ids::irkpg, Ids::ircagp, Library::rate_k39pg(temperature, density, tf));
    store_pair(r, Ids::ircaag, Ids::irtiga, Library::rate_ca40ag(temperature, density, tf));
    store_pair(r, Ids::ircaap, Ids::irscpa, Library::rate_ca40ap(temperature, density, tf));
    store_pair(r, Ids::irscpg, Ids::irtigp, Library::rate_sc43pg(temperature, density, tf));
    store_pair(r, Ids::irtiag, Ids::ircrga, Library::rate_ti44ag(temperature, density, tf));
    store_pair(r, Ids::irtiap, Ids::irvpa, Library::rate_ti44ap(temperature, density, tf));
    store_pair(r, Ids::irvpg, Ids::ircrgp, Library::rate_v47pg(temperature, density, tf));
    store_pair(r, Ids::ircrag, Ids::irfega, Library::rate_cr48ag(temperature, density, tf));
    store_pair(r, Ids::ircrap, Ids::irmnpa, Library::rate_cr48ap(temperature, density, tf));
    store_pair(r, Ids::irmnpg, Ids::irfegp, Library::rate_mn51pg(temperature, density, tf));
    store_pair(r, Ids::irfeag, Ids::irniga, Library::rate_fe52ag(temperature, density, tf));
    store_pair(r, Ids::irfeap, Ids::ircopa, Library::rate_fe52ap(temperature, density, tf));
    store_pair(r, Ids::ircopg, Ids::irnigp, Library::rate_co55pg(temperature, density, tf));
}

template <typename Ids, typename Library, typename Array>
inline void fill_extended_rates(Array& r, double temperature, double density,
                                const TfactorsData& tf)
{
    r[Ids::irpp] = Library::rate_pp(temperature, density, tf).forward;
    store_pair(r, Ids::irhng, Ids::irdgn, Library::rate_png(temperature, density, tf));
    store_pair(r, Ids::irdpg, Ids::irhegp, Library::rate_dpg(temperature, density, tf));
    store_pair(r, Ids::irheng, Ids::irhegn, Library::rate_he3ng(temperature, density, tf));
    r[Ids::ir33] = Library::rate_he3he3(temperature, density, tf).forward;
    r[Ids::irhe3ag] = Library::rate_he3he4(temperature, density, tf).forward;
    r[Ids::ircpg] = Library::rate_c12pg(temperature, density, tf).forward;
    r[Ids::irnpg] = Library::rate_n14pg(temperature, density, tf).forward;

    const double n15pg = Library::rate_n15pg(temperature, density, tf).forward;
    const double n15pa = Library::rate_n15pa(temperature, density, tf).forward;
    const double total = n15pg + n15pa;
    r[Ids::ifa] = n15pa / total;
    r[Ids::ifg] = 1.0 - r[Ids::ifa];

    r[Ids::iropg] = Library::rate_o16pg(temperature, density, tf).forward;
    r[Ids::irnag] = Library::rate_n14ag(temperature, density, tf).forward;
    store_pair(r, Ids::ir52ng, Ids::ir53gn, Library::rate_fe52ng(temperature, density, tf));
    store_pair(r, Ids::ir53ng, Ids::ir54gn, Library::rate_fe53ng(temperature, density, tf));
    store_pair(r, Ids::irfepg, Ids::ircogp, Library::rate_fe54pg(temperature, density, tf));
}

template <typename Ids, typename Library, typename Array>
inline void fill_aprox21_extra_rates(Array& r, double temperature, double density,
                                     const TfactorsData& tf)
{
    store_pair(r, Ids::ir54ng, Ids::ir55gn, Library::rate_fe54ng(temperature, density, tf));
    store_pair(r, Ids::irfe54ap, Ids::irco57pa, Library::rate_fe54ap(temperature, density, tf));
    store_pair(r, Ids::ir55ng, Ids::ir56gn, Library::rate_fe55ng(temperature, density, tf));
    store_pair(r, Ids::irfe56pg, Ids::irco57gp, Library::rate_fe56pg(temperature, density, tf));
}

template <typename Scalar, typename Array>
inline void multiply_rates(Array& rates, const Scalar& factor,
                           std::initializer_list<int> indices)
{
    for (int index : indices) rates[index] *= factor;
}

template <typename Ids, typename Scalar, typename Array>
inline void screen_heavy_rates(Array& r, double temperature, double density,
                               const Scalar& zbar, const Scalar& abar, const Scalar& z2bar)
{
    auto factor = [&](double z1, double a1, double z2, double a2) {
        return screen5(temperature, density, zbar, abar, z2bar, z1, a1, z2, a2);
    };

    const Scalar screen_aa = factor(2.0, 4.0, 2.0, 4.0);
    const Scalar screen_a_be8 = factor(2.0, 4.0, 4.0, 8.0);
    multiply_rates(r, screen_aa * screen_a_be8, {Ids::ir3a, Ids::irg3a});
    multiply_rates(r, factor(6.0, 12.0, 2.0, 4.0), {Ids::ircag, Ids::iroga});
    multiply_rates(r, factor(6.0, 12.0, 6.0, 12.0), {Ids::ir1212});
    multiply_rates(r, factor(6.0, 12.0, 8.0, 16.0), {Ids::ir1216});
    multiply_rates(r, factor(8.0, 16.0, 8.0, 16.0), {Ids::ir1616});
    multiply_rates(r, factor(8.0, 16.0, 2.0, 4.0), {Ids::iroag, Ids::irnega});
    multiply_rates(r, factor(10.0, 20.0, 2.0, 4.0), {Ids::irneag, Ids::irmgga});

    multiply_rates(r, factor(12.0, 24.0, 2.0, 4.0),
                   {Ids::irmgag, Ids::irsiga, Ids::irmgap, Ids::iralpa});
    multiply_rates(r, factor(13.0, 27.0, 1.0, 1.0), {Ids::iralpg, Ids::irsigp});
    multiply_rates(r, factor(14.0, 28.0, 2.0, 4.0),
                   {Ids::irsiag, Ids::irsga, Ids::irsiap, Ids::irppa});
    multiply_rates(r, factor(15.0, 31.0, 1.0, 1.0), {Ids::irppg, Ids::irsgp});
    multiply_rates(r, factor(16.0, 32.0, 2.0, 4.0),
                   {Ids::irsag, Ids::irarga, Ids::irsap, Ids::irclpa});
    multiply_rates(r, factor(17.0, 35.0, 1.0, 1.0), {Ids::irclpg, Ids::irargp});
    multiply_rates(r, factor(18.0, 36.0, 2.0, 4.0),
                   {Ids::irarag, Ids::ircaga, Ids::irarap, Ids::irkpa});
    multiply_rates(r, factor(19.0, 39.0, 1.0, 1.0), {Ids::irkpg, Ids::ircagp});
    multiply_rates(r, factor(20.0, 40.0, 2.0, 4.0),
                   {Ids::ircaag, Ids::irtiga, Ids::ircaap, Ids::irscpa});
    multiply_rates(r, factor(21.0, 43.0, 1.0, 1.0), {Ids::irscpg, Ids::irtigp});
    multiply_rates(r, factor(22.0, 44.0, 2.0, 4.0),
                   {Ids::irtiag, Ids::ircrga, Ids::irtiap, Ids::irvpa});
    multiply_rates(r, factor(23.0, 47.0, 1.0, 1.0), {Ids::irvpg, Ids::ircrgp});
    multiply_rates(r, factor(24.0, 48.0, 2.0, 4.0),
                   {Ids::ircrag, Ids::irfega, Ids::ircrap, Ids::irmnpa});
    multiply_rates(r, factor(25.0, 51.0, 1.0, 1.0), {Ids::irmnpg, Ids::irfegp});
    multiply_rates(r, factor(26.0, 52.0, 2.0, 4.0),
                   {Ids::irfeag, Ids::irniga, Ids::irfeap, Ids::ircopa});
    multiply_rates(r, factor(27.0, 55.0, 1.0, 1.0), {Ids::ircopg, Ids::irnigp});
}

template <typename Ids, typename Scalar, typename Array>
inline void screen_extended_rates(Array& r, double temperature, double density,
                                  const Scalar& zbar, const Scalar& abar, const Scalar& z2bar)
{
    auto factor = [&](double z1, double a1, double z2, double a2) {
        return screen5(temperature, density, zbar, abar, z2bar, z1, a1, z2, a2);
    };
    multiply_rates(r, factor(1.0, 2.0, 1.0, 1.0), {Ids::irdpg, Ids::irhegp});
    multiply_rates(r, factor(1.0, 1.0, 1.0, 1.0), {Ids::irpp});
    multiply_rates(r, factor(2.0, 3.0, 2.0, 3.0), {Ids::ir33});
    multiply_rates(r, factor(2.0, 3.0, 2.0, 4.0), {Ids::irhe3ag});
    multiply_rates(r, factor(6.0, 12.0, 1.0, 1.0), {Ids::ircpg});
    multiply_rates(r, factor(7.0, 14.0, 1.0, 1.0), {Ids::irnpg});
    multiply_rates(r, factor(8.0, 16.0, 1.0, 1.0), {Ids::iropg});
    multiply_rates(r, factor(7.0, 14.0, 2.0, 4.0), {Ids::irnag});
    multiply_rates(r, factor(26.0, 54.0, 1.0, 1.0), {Ids::irfepg, Ids::ircogp});
}

template <typename Ids, typename Scalar, typename Array>
inline void screen_aprox21_extra_rates(Array& r, double temperature, double density,
                                       const Scalar& zbar, const Scalar& abar, const Scalar& z2bar)
{
    auto factor = [&](double z1, double a1, double z2, double a2) {
        return screen5(temperature, density, zbar, abar, z2bar, z1, a1, z2, a2);
    };
    multiply_rates(r, factor(26.0, 54.0, 2.0, 4.0), {Ids::irfe54ap, Ids::irco57pa});
    multiply_rates(r, factor(26.0, 56.0, 1.0, 1.0), {Ids::irfe56pg, Ids::irco57gp});
}

template <typename Scalar, typename Array>
inline void set_branch_ratio(Array& rates, int output, int numerator, int other)
{
    rates[output] = Scalar(0.0);
    const Scalar denominator = rates[numerator] + rates[other];
    if (value_of(denominator) > 1.0e-50) rates[output] = rates[numerator] / denominator;
}

template <bool IncludeFe52, typename Ids, typename Scalar, typename Array>
inline void form_alpha_branch_ratios(Array& r)
{
    set_branch_ratio<Scalar>(r, Ids::irr1, Ids::iralpa, Ids::iralpg);
    set_branch_ratio<Scalar>(r, Ids::irs1, Ids::irppa, Ids::irppg);
    set_branch_ratio<Scalar>(r, Ids::irt1, Ids::irclpa, Ids::irclpg);
    set_branch_ratio<Scalar>(r, Ids::iru1, Ids::irkpa, Ids::irkpg);
    set_branch_ratio<Scalar>(r, Ids::irv1, Ids::irscpa, Ids::irscpg);
    set_branch_ratio<Scalar>(r, Ids::irw1, Ids::irvpa, Ids::irvpg);
    set_branch_ratio<Scalar>(r, Ids::irx1, Ids::irmnpa, Ids::irmnpg);
    if constexpr (IncludeFe52) {
        set_branch_ratio<Scalar>(r, Ids::iry1, Ids::ircopa, Ids::ircopg);
    }
}

template <typename Scalar>
inline Scalar scalar_min(const Scalar& a, const Scalar& b)
{
    return value_of(a) < value_of(b) ? a : b;
}

template <bool Aprox21, typename Ids, typename Scalar, typename Array>
inline void form_extended_equilibrium(Array& r, const Scalar* y,
                                      int ihe4, int ih1, int ineut, int iprot,
                                      double temperature)
{
    form_alpha_branch_ratios<false, Ids, Scalar>(r);

    r[Ids::ir1f54] = r[Ids::ir2f54] = Scalar(0.0);
    Scalar denominator = r[Ids::ir53gn] + y[ineut] * r[Ids::ir53ng];
    if (value_of(denominator) > 1.0e-50 && temperature > 1.5e9) {
        r[Ids::ir1f54] = r[Ids::ir54gn] * r[Ids::ir53gn] / denominator;
        r[Ids::ir2f54] = r[Ids::ir52ng] * r[Ids::ir53ng] / denominator;
    }

    r[Ids::ir3f54] = r[Ids::ir4f54] = r[Ids::ir5f54] = r[Ids::ir6f54] = Scalar(0.0);
    r[Ids::ir7f54] = r[Ids::ir8f54] = Scalar(0.0);
    denominator = r[Ids::ircogp] + y[iprot] * (r[Ids::ircopg] + r[Ids::ircopa]);
    if (value_of(denominator) > 1.0e-50 && temperature > 1.5e9) {
        r[Ids::ir3f54] = r[Ids::irfepg] * r[Ids::ircopg] / denominator;
        r[Ids::ir4f54] = r[Ids::irnigp] * r[Ids::ircogp] / denominator;
        r[Ids::ir5f54] = r[Ids::irfepg] * r[Ids::ircopa] / denominator;
        r[Ids::ir6f54] = r[Ids::irfeap] * r[Ids::ircogp] / denominator;
        r[Ids::ir7f54] = r[Ids::irfeap] * r[Ids::ircopg] / denominator;
        r[Ids::ir8f54] = r[Ids::irnigp] * r[Ids::ircopa] / denominator;
    }

    r[Ids::iralf1] = r[Ids::iralf2] = Scalar(0.0);
    denominator = r[Ids::irhegp] * r[Ids::irdgn]
                + y[ineut] * r[Ids::irheng] * r[Ids::irdgn]
                + y[ineut] * y[iprot] * r[Ids::irheng] * r[Ids::irdpg];
    if (value_of(denominator) > 1.0e-50 && temperature > 1.5e9) {
        r[Ids::iralf1] = r[Ids::irhegn] * r[Ids::irhegp] * r[Ids::irdgn] / denominator;
        r[Ids::iralf2] = r[Ids::irheng] * r[Ids::irdpg] * r[Ids::irhng] / denominator;
    }

    if constexpr (Aprox21) {
        r[Ids::irfe56_aux1] = r[Ids::irfe56_aux2] = Scalar(0.0);
        denominator = r[Ids::ir55gn] + y[ineut] * r[Ids::ir55ng];
        if (value_of(denominator) > 1.0e-50 && temperature > 1.5e9) {
            r[Ids::irfe56_aux1] = r[Ids::ir56gn] * r[Ids::ir55gn] / denominator;
            r[Ids::irfe56_aux2] = r[Ids::ir54ng] * r[Ids::ir55ng] / denominator;
        }

        r[Ids::irfe56_aux3] = r[Ids::irfe56_aux4] = Scalar(0.0);
        denominator = r[Ids::irco57gp] + y[iprot] * r[Ids::irco57pa];
        if (value_of(denominator) > 1.0e-50 && temperature > 1.5e9) {
            r[Ids::irfe56_aux3] = r[Ids::irfe56pg] * r[Ids::irco57pa] / denominator;
            r[Ids::irfe56_aux4] = r[Ids::irfe54ap] * r[Ids::irco57gp] / denominator;
        }
    }

    if (value_of(y[ihe4]) > 1.0e-30) {
        r[Ids::irhe3ag] = scalar_min(r[Ids::irhe3ag], Scalar(0.896) / y[ihe4]);
    }
    if (value_of(y[ih1]) > 1.0e-30) {
        r[Ids::irnpg] = scalar_min(r[Ids::irnpg], Scalar(5.68e-3) / (y[ih1] * 1.57));
        r[Ids::iropg] = scalar_min(r[Ids::iropg], Scalar(0.0105) / y[ih1]);
    }
}

} // namespace timmes
