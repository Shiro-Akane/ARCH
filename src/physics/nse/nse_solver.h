/**
 * @file nse_solver.h
 * @brief Network-constrained nuclear statistical equilibrium solver.
 *
 * Workflow: validate nuclear data and the physical state; build conditioned
 * Saha logarithms; retain a residual-certified fixed point or solve the mass/
 * charge constraints; return the original nuclear-energy contraction only
 * after conservation succeeds. Host and CUDA share the same arithmetic.
 * @note The Saha formulation, mass/charge residuals, and two-potential Newton
 * method are adapted from Frank Timmes's public_nse package at
 * https://cococubed.com/code_pages/nse.shtml. ARCH adds the generic NetType
 * interface, numerical safeguards, compact-network handling, and energy
 * closure described in docs/physics/TimmesNetworks.md.
 */
#pragma once

#include "core/MathClassification.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

#include "core/ArchPortability.h"
#include "core/CompensatedSum.h"
#include "physics/constant/PhysicalConstants.h"
#include "physics/network/NuclearEnergy.h"

namespace arch::nse_detail {

ARCH_HOST_DEVICE inline bool is_finite_bits(double value)
{
#if defined(__CUDA_ARCH__)
    const auto bits = static_cast<unsigned long long>(__double_as_longlong(value));
#else
    const auto bits = std::bit_cast<std::uint64_t>(value);
#endif
    return (bits & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}

ARCH_HOST_DEVICE inline double clamp_by_value(double value, double lower,
                                               double upper)
{
    return value < lower ? lower : (value > upper ? upper : value);
}

} // namespace arch::nse_detail

/**
 * @brief Timmes-derived nuclear statistical equilibrium solver.
 *
 * NetType must provide the following compile-time data:
 *
 *   NUM_SPECIES              number of entries in the network
 *   aion(i), zion(i)         mass and charge number accessors
 *   binding_energy(i)        total nuclear binding energy in MeV
 *   spin_weight(i)           partition/statistical-weight accessor
 *   ENERGY_CONVERSION        MeV per molar-abundance unit -> erg/g
 *
 * The checked-in network AION/ZION/BINDING_E/SPIN arrays remain the host
 * authority behind these POD accessors.  A non-positive spin_weight entry
 * excludes a network pseudo-species from NSE.  This is useful for approximate
 * networks which contain two bookkeeping entries for the same physical
 * nuclide.  A NetType may additionally provide NSE_ENERGY_CONVERSION when its
 * normal reaction-network energy convention is based on nuclear rest masses
 * rather than binding energies.
 *
 * Generated packages advertise NSE_DATA_VERSION and supply their original
 * pynucastro mass-number and constant conventions for the Saha prefactor.
 * Their energy uses the same mass contraction as the kinetic network. The
 * built-in data path keeps its original prefactor and binding-energy closure.
 *
 * The public interface uses mass fractions X despite the historical Y names.
 * Internally the Saha equation and energy closure use molar abundance X/A.
 */

namespace arch::nse_detail {

// These elementary functions are private to the Saha solve. A platform-libm
// ulp in a large cancelling log or capped chemical-potential step can become
// a much larger relative error in a small nuclear-energy increment. Keep
// their arithmetic shared, without changing the equilibrium equations or
// the original residual/conservation/energy acceptance criteria.
inline constexpr double log_two = 0x1.62e42fefa39efp-1;
inline constexpr double log_two_tail = 0x1.abc9e3b39803fp-56;
inline constexpr double inverse_log_two = 0x1.71547652b82fep+0;
inline constexpr double sqrt_half = 0x1.6a09e667f3bcdp-1;

/** x=m*2^e, m in [sqrt(1/2),sqrt(2)]. With t=(m-1)/(m+1),
 * log(m)=2*t*sum_{k=0}^{13} t^(2k)/(2k+1). The omitted atanh tail is
 * below 4.474e-24. Compensated reconstruction retains the low part of
 * log(2); the constants above are its nearest double and remaining tail.
 * Invalid/nonfinite inputs keep their standard mathematical classification.
 */
ARCH_HOST_DEVICE inline double logarithm(double value)
{
    if (!(value > 0.0) || !arch::math::isfinite(value)) return std::log(value);
    int exponent = 0;
    double m = std::frexp(value, &exponent);
    if (m < sqrt_half) {
        m *= 2.0;
        --exponent;
    }
    const double t = (m - 1.0) / (m + 1.0);
    const double t_squared = t * t;
    constexpr int terms = 14; // Odd powers through t^27.
    double polynomial = 1.0 / (2 * terms - 1);
    for (int k = terms - 2; k >= 0; --k)
        polynomial = std::fma(polynomial, t_squared, 1.0 / (2 * k + 1));
    arch::math::CompensatedSum result;
    result.add((2.0 * t) * polynomial);
    result.add_product(static_cast<double>(exponent), log_two);
    result.add_product(static_cast<double>(exponent), log_two_tail);
    return result.value();
}

/** x=n*log(2)+r, |r|<=log(2)/2 up to rounding. The coefficients below
 * are the rounded 1/k! values for k=0..18; the degree-18 Taylor remainder
 * is below 2.1e-26. Explicit FMAs keep the polynomial order identical.
 * This is selected Saha arithmetic, not a universal correctly-rounded libm.
 */
ARCH_HOST_DEVICE inline double exponential(double value)
{
    if (!arch::math::isfinite(value)) return std::exp(value);
    // exp(-746)<denorm_min/2 and exp(710)>DBL_MAX. These outer bounds
    // protect integer reduction; ldexp rounds actual subnormal/overflow edges.
    if (value <= -746.0) return 0.0;
    if (value >= 710.0) return std::numeric_limits<double>::infinity();
    const int n = static_cast<int>(std::floor(
        std::fma(value, inverse_log_two, 0.5)));
    double r = std::fma(-static_cast<double>(n), log_two, value);
    r = std::fma(-static_cast<double>(n), log_two_tail, r);
    constexpr double coefficients[19]{
        0x1p+0, 0x1p+0, 0x1p-1,
        0x1.5555555555555p-3, 0x1.5555555555555p-5,
        0x1.1111111111111p-7, 0x1.6c16c16c16c17p-10,
        0x1.a01a01a01a01ap-13, 0x1.a01a01a01a01ap-16,
        0x1.71de3a556c734p-19, 0x1.27e4fb7789f5cp-22,
        0x1.ae64567f544e4p-26, 0x1.1eed8eff8d898p-29,
        0x1.6124613a86d09p-33, 0x1.93974a8c07c9dp-37,
        0x1.ae7f3e733b81fp-41, 0x1.ae7f3e733b81fp-45,
        0x1.952c77030ad4ap-49, 0x1.6827863b97d97p-53
    };
    double polynomial = coefficients[18];
    for (int k = 17; k >= 0; --k)
        polynomial = std::fma(polynomial, r, coefficients[k]);
    return std::ldexp(polynomial, n);
}

/** Euclidean step length for finite chemical-potential corrections.
 * Scaling by the largest component avoids squaring large corrections.
 * sqrt is shared IEEE arithmetic; a platform hypot must not choose a
 * different capped Newton direction before the original line search.
 */
ARCH_HOST_DEVICE inline double norm2(double a, double b)
{
    const double hi = std::max(std::abs(a), std::abs(b));
    if (hi == 0.0) return 0.0;
    const double ratio = std::min(std::abs(a), std::abs(b)) / hi;
    return hi * std::sqrt(1.0 + ratio * ratio);
}
} // namespace arch::nse_detail

template <typename NetType>
struct NSESolver
{
    static constexpr int NUM_SPEC = NetType::NUM_SPECIES;
    static_assert(NUM_SPEC > 0, "NSE requires at least one network species");
    static constexpr bool generated_data = requires { NetType::NSE_DATA_VERSION; };

    ARCH_HOST_DEVICE static constexpr double minimum_temperature()
    {
        if constexpr (generated_data) return NetType::NSE_T_MIN;
        else return 0.0;
    }

    ARCH_HOST_DEVICE static constexpr double maximum_temperature()
    {
        if constexpr (generated_data) return NetType::NSE_T_MAX;
        else return std::numeric_limits<double>::infinity();
    }

    /**
     * @brief Solve the network-constrained NSE state at fixed T, rho and Ye.
     * @param T       temperature [K]
     * @param rho     mass density [g cm^-3]
     * @param Ye      electron fraction, sum_i (Z_i/A_i) X_i
     * @param X_old   input mass fractions (kept unchanged)
     * @param X_out   output NSE mass fractions
     * @param enuc    nuclear-energy change from X_old to X_out [erg g^-1]
     * @return true only after Newton convergence and an independent
     *         mass/charge-conservation check
     */
    ARCH_HEAVY_INLINE static bool solve(double T, double rho, double Ye,
                                       const double* X_old, double* X_out,
                                       double& enuc)
    {
        enuc = 0.0;
        if (X_old == nullptr || X_out == nullptr || !arch::math::isfinite(T)
            || !arch::math::isfinite(rho) || !arch::math::isfinite(Ye)
            || T <= 0.0 || rho <= 0.0 || Ye < 0.0 || Ye > 1.0) {
            return false;
        }
        if constexpr (generated_data) {
            static_assert(NetType::NSE_DATA_VERSION == 1,
                          "Unsupported generated NSE nuclear-data contract");
            if (T < minimum_temperature() || T > maximum_temperature()) return false;
        }

        std::array<double, NUM_SPEC> log_base{};
        double q_min = std::numeric_limits<double>::infinity();
        double q_max = -std::numeric_limits<double>::infinity();
        int active_count = 0;

        const double kT = k_boltzmann * T;
        const double kT_mev = kT / mev_to_erg;
        if (!arch::math::isfinite(kT_mev) || kT_mev <= 0.0) return false;

        for (int i = 0; i < NUM_SPEC; ++i) {
            if (!arch::nse_detail::is_finite_bits(X_old[i])
                || X_old[i] < -conservation_tol) {
                return false;
            }

            const double A = NetType::aion(i);
            const double Z = NetType::zion(i);
            const double binding = NetType::binding_energy(i);
            const double weight = NetType::spin_weight(i);
            if (!arch::math::isfinite(A) || !arch::math::isfinite(Z)
                || !arch::math::isfinite(binding) || !arch::math::isfinite(weight)
                || A <= 0.0 || Z < 0.0 || Z > A) {
                return false;
            }

            if (weight <= 0.0) {
                log_base[i] = -std::numeric_limits<double>::infinity();
                continue;
            }

            if constexpr (generated_data) {
                // pynucastro's detailed-balance convention uses A_nuc rather
                // than integer A in the translational mass factor. Preserve
                // that package's constants and normalized partition function.
                const double mass = NetType::nse_mass_number(i)
                                  * NetType::NSE_ATOMIC_MASS_UNIT;
                const double partition = NetType::nse_log_partition(i, T);
                if (!arch::math::isfinite(mass) || mass <= 0.0
                    || !arch::math::isfinite(partition)) return false;
                log_base[i] = 2.5 * arch::nse_detail::logarithm(mass) + arch::nse_detail::logarithm(weight) - arch::nse_detail::logarithm(rho)
                    + 1.5 * arch::nse_detail::logarithm(NetType::NSE_K_BOLTZMANN * T
                        / (two_pi * NetType::NSE_HBAR * NetType::NSE_HBAR))
                    + binding / (NetType::NSE_K_BOLTZMANN_MEV * T) + partition;
            } else {
                // Hartmann et al. (1985), ApJ 297, 837, Eq. 2, in the same
                // convention as public_nse.f90. BINDING_E is total MeV per
                // nucleus, not binding energy per nucleon.
                // Combine positive translational factors before log to
                // avoid cancelling two large prefactor logarithms.
                const double nuclear_mass = A * atomic_mass_unit;
                const double quantum = two_pi * nuclear_mass * kT / (planck * planck);
                const double log_prefactor = arch::nse_detail::logarithm(
                    (A * weight / (avogadro * rho) * quantum) * std::sqrt(quantum));
                // Remove A*b_ref/kT from log_base and absorb b_ref/kT
                // into BOTH chemical potentials. N+Z=A makes this the same
                // Saha equation with a smaller intermediate binding scale.
                // Nuclear-energy weights/conversion remain unchanged.
                const int anchor = best_anchor(Ye);
                if (anchor < 0) return false;
                const double gauge = NetType::binding_energy(anchor) / NetType::aion(anchor);
                const double shifted_binding = std::fma(-A, gauge, binding);
                log_base[i] = log_prefactor + shifted_binding / kT_mev;
            }
            if (!arch::math::isfinite(log_base[i])) return false;

            const double q = Z / A;
            q_min = std::min(q_min, q);
            q_max = std::max(q_max, q);
            ++active_count;
        }

        if (active_count == 0 || Ye < q_min - conservation_tol
            || Ye > q_max + conservation_tol) {
            return false;
        }

        std::array<double, NUM_SPEC> solution{};
        bool converged = false;
        const double q_span = q_max - q_min;

        // A nonlinear projection must leave an already converged input fixed.
        // Re-solving it from a cold guess can change a few composition ulps;
        // dividing that artificial binding change by a tiny burn interval
        // then manufactures a source. Accept the INPUT only when it satisfies
        // every Saha relation and the original conservation tolerances. This
        // is a residual test, not a small-energy or small-delta-X cutoff.
        for (int i = 0; i < NUM_SPEC; ++i) solution[i] = X_old[i];
        const bool input_conserved = check_conservation(solution, Ye);
        if constexpr (generated_data) {
            // A mass-based source is gauge-independent only for a conserved
            // increment. Never silently normalize an invalid input or change
            // its charge while interpreting the change as nuclear burning.
            if (!input_conserved) return false;
        }
        if (input_conserved && input_is_equilibrium(log_base, solution, q_span)) {
            for (int i = 0; i < NUM_SPEC; ++i) X_out[i] = solution[i];
            return true; // enuc was initialized to exactly zero.
        }

        // Alpha-chain networks have Z/A = 1/2 for every species.  Their two
        // conservation equations are linearly dependent, so the 2x2
        // Jacobian is singular even though the restricted NSE composition is
        // well defined.  Fix mu_n = mu_p and solve the remaining scalar
        // normalization equation.
        if (q_span <= charge_degeneracy_tol) {
            if (std::abs(Ye - q_min) > conservation_tol) return false;
            converged = solve_degenerate(log_base, Ye, solution);
        } else {
            converged = solve_two_dimensional(log_base, Ye, q_span, solution);
        }

        if (!converged || !check_conservation(solution, Ye)) return false;

        double energy;
        if constexpr (generated_data) {
            energy = arch::network_energy::integrated_composition_energy<NetType>(
                solution.data(), X_old);
        } else {
            arch::math::CompensatedSum delta_binding;
            for (int i = 0; i < NUM_SPEC; ++i) {
                delta_binding.add(
                    (solution[i] - X_old[i])
                    * (NetType::binding_energy(i) / NetType::aion(i)));
            }

            energy = binding_energy_conversion() * delta_binding.value();
        }
        if (!arch::math::isfinite(energy)) return false;

#pragma omp simd
        for (int i = 0; i < NUM_SPEC; ++i) X_out[i] = solution[i];
        enuc = energy;
        return true;
    }

    /** @brief Compile-time line-search policy for host/device contract probes. */
    ARCH_HOST_DEVICE static constexpr int line_search_limit()
    {
        return max_line_search;
    }

private:
    // Shared current physical constants; network binding data retain their own
    // declared conversion below, rather than silently reinterpreting the data.
    static constexpr double two_pi = arch::constants::math::two_pi;
    static constexpr double planck = arch::constants::quantum::cgs::planck;
    static constexpr double avogadro = arch::constants::statistical::avogadro;
    static constexpr double k_boltzmann = arch::constants::statistical::cgs::boltzmann;
    static constexpr double atomic_mass_unit = arch::constants::atomic::cgs::atomic_mass_unit;
    static constexpr double mev_to_erg = arch::constants::units::erg_per_mev;

    static constexpr int max_iterations = 100;
    static constexpr int max_line_search = 24;
    // Stricter than the requested 1e-10 stopping criterion so the translated
    // state remains below the project's 1e-12 physical-validation target.
    static constexpr double residual_tol = 1.0e-12;
    static constexpr double conservation_tol = 1.0e-12;
    static constexpr double charge_degeneracy_tol = 128.0
        * std::numeric_limits<double>::epsilon();
    static constexpr double max_newton_step = 20.0;

    struct Evaluation
    {
        std::array<double, NUM_SPEC> x{};
        double f_mass = 0.0;   // log(sum_i X_i)
        double f_charge = 0.0; // sum_i (Z_i/A_i) X_i - Ye
        double j00 = 0.0;
        double j01 = 0.0;
        double j10 = 0.0;
        double j11 = 0.0;
    };

    ARCH_HOST_DEVICE static constexpr double binding_energy_conversion()
    {
        if constexpr (requires { NetType::NSE_ENERGY_CONVERSION; }) {
            return NetType::NSE_ENERGY_CONVERSION;
        } else {
            return NetType::ENERGY_CONVERSION;
        }
    }

    ARCH_HOST_DEVICE static double log_mass_fraction(
        const std::array<double, NUM_SPEC>& log_base, int i,
        double eta_n, double eta_p)
    {
        // Preserve product low bits when opposite chemical potentials cancel.
        arch::math::CompensatedSum sum;
        sum.add(log_base[i]);
        sum.add_product(NetType::aion(i) - NetType::zion(i), eta_n);
        sum.add_product(NetType::zion(i), eta_p);
        return sum.value();
    }

    ARCH_HOST_DEVICE static bool input_is_equilibrium(
        const std::array<double, NUM_SPEC>& log_base,
        const std::array<double, NUM_SPEC>& x, double q_span)
    {
        int anchor = -1;
        for (int i = 0; i < NUM_SPEC; ++i) {
            if (NetType::spin_weight(i) <= 0.0) {
                if (x[i] != 0.0) return false;
            } else {
                // An underflowed active abundance cannot certify a logarithmic
                // residual. Use the ordinary safeguarded solver in that case.
                if (!(x[i] > 0.0)) return false;
                if (anchor < 0 || x[i] > x[anchor]) anchor = i;
            }
        }
        if (anchor < 0) return false;
        const double anchor_q = NetType::zion(anchor) / NetType::aion(anchor);
        const double anchor_potential = (arch::nse_detail::logarithm(x[anchor]) - log_base[anchor])
                                      / NetType::aion(anchor);
        double eta_n = anchor_potential, eta_p = anchor_potential;
        if (q_span > charge_degeneracy_tol) {
            int partner = -1;
            double best_weight = 0.0;
            for (int i = 0; i < NUM_SPEC; ++i) {
                if (NetType::spin_weight(i) <= 0.0) continue;
                const double q = NetType::zion(i) / NetType::aion(i);
                const double weight = x[i] * std::abs(q - anchor_q);
                if (weight > best_weight) {
                    best_weight = weight;
                    partner = i;
                }
            }
            if (partner < 0) return false;
            const double partner_q = NetType::zion(partner) / NetType::aion(partner);
            const double partner_potential = (arch::nse_detail::logarithm(x[partner]) - log_base[partner])
                                           / NetType::aion(partner);
            const double charge_potential = (partner_potential - anchor_potential)
                                          / (partner_q - anchor_q);
            eta_n = anchor_potential - anchor_q * charge_potential;
            eta_p = eta_n + charge_potential;
        }
        if (!arch::math::isfinite(eta_n) || !arch::math::isfinite(eta_p)) return false;
        for (int i = 0; i < NUM_SPEC; ++i) {
            if (NetType::spin_weight(i) <= 0.0) continue;
            const double residual = arch::nse_detail::logarithm(x[i])
                                  - log_mass_fraction(log_base, i, eta_n, eta_p);
            if (!arch::math::isfinite(residual) || std::abs(residual) > residual_tol)
                return false;
        }
        return true;
    }

    ARCH_HOST_DEVICE static bool evaluate(
        const std::array<double, NUM_SPEC>& log_base,
        double eta_n, double eta_p, double Ye, Evaluation& out)
    {
        std::array<double, NUM_SPEC> log_x{};
        double max_log_x = -std::numeric_limits<double>::infinity();

        for (int i = 0; i < NUM_SPEC; ++i) {
            if (NetType::spin_weight(i) > 0.0) {
                log_x[i] = log_mass_fraction(log_base, i, eta_n, eta_p);
                max_log_x = std::max(max_log_x, log_x[i]);
            } else {
                log_x[i] = -std::numeric_limits<double>::infinity();
            }
        }
        if (!arch::math::isfinite(max_log_x)) return false;

        // Center charge BEFORE summing; subtracting <q>-Ye afterwards
        // loses the small residual. Centered covariances give the same
        // Jacobian. Both backends use this compensated species order.
        arch::math::CompensatedSum acc_w;
        arch::math::CompensatedSum acc_n;
        arch::math::CompensatedSum acc_z;
        arch::math::CompensatedSum acc_q_offset;
        arch::math::CompensatedSum acc_qn_offset;
        arch::math::CompensatedSum acc_qz_offset;
        for (int i = 0; i < NUM_SPEC; ++i) {
            const double w = NetType::spin_weight(i) > 0.0
                           ? arch::nse_detail::exponential(log_x[i] - max_log_x) : 0.0;
            const double A = NetType::aion(i);
            const double Z = NetType::zion(i);
            const double N = A - Z;
            const double q = Z / A;
            out.x[i] = w;
            acc_w.add(w);
            acc_n.add(w * N);
            acc_z.add(w * Z);
            acc_q_offset.add(w * (q - Ye));
            acc_qn_offset.add(w * (q - Ye) * N);
            acc_qz_offset.add(w * (q - Ye) * Z);
        }
        const double sum_w = acc_w.value();
        const double sum_n = acc_n.value();
        const double sum_z = acc_z.value();
        const double sum_q_offset = acc_q_offset.value();
        const double sum_qn_offset = acc_qn_offset.value();
        const double sum_qz_offset = acc_qz_offset.value();
        if (!arch::math::isfinite(sum_w) || sum_w <= 0.0) return false;

        const double inv_sum = 1.0 / sum_w;
        const double mean_n = sum_n * inv_sum;
        const double mean_z = sum_z * inv_sum;
        const double mean_q_offset = sum_q_offset * inv_sum;

#pragma omp simd
        for (int i = 0; i < NUM_SPEC; ++i) out.x[i] *= inv_sum;

        // Solving log(sum X)=0 instead of sum X-1=0 is algebraically
        // equivalent at the root and avoids both overflow and underflow.
        out.f_mass = max_log_x + arch::nse_detail::logarithm(sum_w);
        out.f_charge = mean_q_offset;
        out.j00 = mean_n;
        out.j01 = mean_z;
        out.j10 = sum_qn_offset * inv_sum - mean_q_offset * mean_n;
        out.j11 = sum_qz_offset * inv_sum - mean_q_offset * mean_z;

        return arch::math::isfinite(out.f_mass) && arch::math::isfinite(out.f_charge)
            && arch::math::isfinite(out.j00) && arch::math::isfinite(out.j01)
            && arch::math::isfinite(out.j10) && arch::math::isfinite(out.j11);
    }

    ARCH_HOST_DEVICE static double objective(const Evaluation& e,
                                              double q_span)
    {
        // q_span is a dimensionless charge-per-baryon range. Its 1e-3 floor
        // scales the line-search objective when species have similar charge;
        // final equilibrium/conservation checks retain their separate gates.
        const double scaled_charge = e.f_charge / std::max(q_span, 1.0e-3);
        return 0.5 * (e.f_mass * e.f_mass
                    + scaled_charge * scaled_charge);
    }

    ARCH_HOST_DEVICE static bool solve_two_dimensional(
        const std::array<double, NUM_SPEC>& log_base, double Ye,
        double q_span, std::array<double, NUM_SPEC>& solution)
    {
        for (int attempt = 0; attempt < 3; ++attempt) {
            double eta_n = 0.0;
            double eta_p = 0.0;
            if (!initial_guess(log_base, Ye, attempt, eta_n, eta_p)) continue;

            for (int iter = 0; iter < max_iterations; ++iter) {
                Evaluation current;
                if (!evaluate(log_base, eta_n, eta_p, Ye, current)) break;
                if (std::abs(current.f_mass) < residual_tol
                    && std::abs(current.f_charge) < residual_tol) {
                    solution = current.x;
                    return true;
                }

                const double det = current.j00 * current.j11
                                 - current.j01 * current.j10;
                const double det_scale = std::abs(current.j00 * current.j11)
                                       + std::abs(current.j01 * current.j10);
                if (!arch::math::isfinite(det)
                    || std::abs(det) <= 128.0
                       * std::numeric_limits<double>::epsilon()
                       * std::max(det_scale,
                                  std::numeric_limits<double>::min())) {
                    break;
                }

                // Cramer's rule for J delta = -f.
                double delta_n = (-current.f_mass * current.j11
                                  + current.j01 * current.f_charge) / det;
                double delta_p = (-current.j00 * current.f_charge
                                  + current.f_mass * current.j10) / det;
                if (!arch::math::isfinite(delta_n) || !arch::math::isfinite(delta_p)) break;

                const double step_norm = arch::nse_detail::norm2(delta_n, delta_p);
                if (step_norm > max_newton_step) {
                    const double scale = max_newton_step / step_norm;
                    delta_n *= scale;
                    delta_p *= scale;
                }

                const double old_objective = objective(current, q_span);
                bool accepted = false;
                double alpha = 1.0;
                for (int ls = 0; ls < max_line_search; ++ls) {
                    Evaluation trial;
                    if (evaluate(log_base, eta_n + alpha * delta_n,
                                 eta_p + alpha * delta_p, Ye, trial)
                        && objective(trial, q_span) < old_objective) {
                        eta_n += alpha * delta_n;
                        eta_p += alpha * delta_p;
                        accepted = true;
                        break;
                    }
                    alpha *= 0.5;
                }
                if (!accepted) break;
            }
        }

        // A highly proton- or neutron-rich restricted network can make the
        // 2x2 determinant tiny even though a physical solution exists.  Keep
        // Timmes' 2x2 Newton method as the fast path, then fall back to a
        // globally bracketed solve in the charge chemical potential.  For a
        // fixed eta_p-eta_n, mass normalization is a monotone scalar problem.
        return solve_charge_bisection(log_base, Ye, solution);
    }

    ARCH_HOST_DEVICE static bool normalized_at_charge_potential(
        const std::array<double, NUM_SPEC>& log_base, double Ye,
        double charge_potential, Evaluation& result)
    {
        const int anchor = best_anchor(Ye);
        if (anchor < 0) return false;

        // log X_i = log_base_i + A_i eta_n
        //                         + Z_i (eta_p-eta_n).
        // Set the anchor abundance to order unity for the scalar initial
        // guess, then solve log(sum X)=0 analytically with d/deta_n=<A>.
        double eta_n =
            -(log_base[anchor]
              + NetType::zion(anchor) * charge_potential)
            / NetType::aion(anchor);
        for (int iter = 0; iter < max_iterations; ++iter) {
            if (!evaluate(log_base, eta_n, eta_n + charge_potential,
                          Ye, result)) {
                return false;
            }
            if (std::abs(result.f_mass) < residual_tol) return true;
            const double mean_a = result.j00 + result.j01;
            if (!arch::math::isfinite(mean_a) || mean_a <= 0.0) return false;
            eta_n += arch::nse_detail::clamp_by_value(
                -result.f_mass / mean_a, -max_newton_step, max_newton_step);
        }
        return false;
    }

    ARCH_HOST_DEVICE static bool solve_charge_bisection(
        const std::array<double, NUM_SPEC>& log_base, double Ye,
        std::array<double, NUM_SPEC>& solution)
    {
        Evaluation center;
        if (!normalized_at_charge_potential(log_base, Ye, 0.0, center)) {
            return false;
        }
        if (std::abs(center.f_charge) < residual_tol) {
            solution = center.x;
            return true;
        }

        double lower_potential = 0.0;
        double upper_potential = 0.0;
        Evaluation lower = center;
        Evaluation upper = center;
        const double direction = center.f_charge < 0.0 ? 1.0 : -1.0;
        double step = direction;
        bool bracketed = false;

        for (int expansion = 0; expansion < 32; ++expansion) {
            const double potential = direction > 0.0
                                   ? upper_potential + step
                                   : lower_potential + step;
            Evaluation trial;
            if (!normalized_at_charge_potential(
                    log_base, Ye, potential, trial)) {
                return false;
            }
            if (direction > 0.0) {
                upper_potential = potential;
                upper = trial;
                bracketed = upper.f_charge >= 0.0;
            } else {
                lower_potential = potential;
                lower = trial;
                bracketed = lower.f_charge <= 0.0;
            }
            if (bracketed) break;
            step *= 2.0;
        }
        if (!bracketed) return false;

        // Ensure lower has negative charge residual and upper positive.
        if (lower.f_charge > 0.0) {
            std::swap(lower, upper);
            std::swap(lower_potential, upper_potential);
        }

        for (int iter = 0; iter < 160; ++iter) {
            const double midpoint_potential =
                0.5 * (lower_potential + upper_potential);
            Evaluation midpoint;
            if (!normalized_at_charge_potential(
                    log_base, Ye, midpoint_potential, midpoint)) {
                return false;
            }
            if (std::abs(midpoint.f_charge) < residual_tol) {
                solution = midpoint.x;
                return true;
            }
            if (midpoint.f_charge < 0.0) {
                lower_potential = midpoint_potential;
                lower = midpoint;
            } else {
                upper_potential = midpoint_potential;
                upper = midpoint;
            }
        }
        return false;
    }

    ARCH_HOST_DEVICE static bool solve_degenerate(
        const std::array<double, NUM_SPEC>& log_base,
        double Ye,
        std::array<double, NUM_SPEC>& solution)
    {
        int anchor = best_anchor(Ye);
        if (anchor < 0) return false;
        double eta = -log_base[anchor] / NetType::aion(anchor);

        for (int iter = 0; iter < max_iterations; ++iter) {
            double max_log_x = -std::numeric_limits<double>::infinity();
            std::array<double, NUM_SPEC> log_x{};
            for (int i = 0; i < NUM_SPEC; ++i) {
                if (NetType::spin_weight(i) > 0.0) {
                    log_x[i] = log_base[i] + NetType::aion(i) * eta;
                    max_log_x = std::max(max_log_x, log_x[i]);
                } else {
                    log_x[i] = -std::numeric_limits<double>::infinity();
                }
            }
            if (!arch::math::isfinite(max_log_x)) return false;

            arch::math::CompensatedSum acc_w, acc_aw;
            for (int i = 0; i < NUM_SPEC; ++i) {
                const double w = NetType::spin_weight(i) > 0.0
                               ? arch::nse_detail::exponential(log_x[i] - max_log_x) : 0.0;
                solution[i] = w;
                acc_w.add(w);
                acc_aw.add(w * NetType::aion(i));
            }
            const double sum_w = acc_w.value();
            const double sum_aw = acc_aw.value();
            if (!arch::math::isfinite(sum_w) || sum_w <= 0.0) return false;

            const double f = max_log_x + arch::nse_detail::logarithm(sum_w);
            const double mean_a = sum_aw / sum_w;
            if (std::abs(f) < residual_tol) {
                const double inv_sum = 1.0 / sum_w;
#pragma omp simd
                for (int i = 0; i < NUM_SPEC; ++i) solution[i] *= inv_sum;
                return true;
            }
            if (!arch::math::isfinite(mean_a) || mean_a <= 0.0) return false;
            const double delta = arch::nse_detail::clamp_by_value(
                -f / mean_a, -max_newton_step, max_newton_step);
            eta += delta;
        }
        return false;
    }

    ARCH_HOST_DEVICE static int best_anchor(double Ye)
    {
        int anchor = -1;
        double best_charge_distance = std::numeric_limits<double>::infinity();
        double best_binding_per_nucleon =
            -std::numeric_limits<double>::infinity();
        for (int i = 0; i < NUM_SPEC; ++i) {
            if (NetType::spin_weight(i) <= 0.0) continue;
            const double charge_distance =
                std::abs(NetType::zion(i) / NetType::aion(i) - Ye);
            const double binding_per_nucleon =
                NetType::binding_energy(i) / NetType::aion(i);
            if (charge_distance < best_charge_distance
                || (charge_distance == best_charge_distance
                    && binding_per_nucleon > best_binding_per_nucleon)) {
                anchor = i;
                best_charge_distance = charge_distance;
                best_binding_per_nucleon = binding_per_nucleon;
            }
        }
        return anchor;
    }

    ARCH_HOST_DEVICE static bool initial_guess(
        const std::array<double, NUM_SPEC>& log_base, double Ye, int attempt,
        double& eta_n, double& eta_p)
    {
        const int anchor = best_anchor(Ye);
        if (anchor < 0) return false;
        const double eta = -log_base[anchor] / NetType::aion(anchor);

        if (attempt == 0) {
            // Generic counterpart of Timmes' Ni56 initial guess: select the
            // most tightly bound species whose Z/A is closest to the target
            // Ye, set X_anchor=1, and start with mu_n=mu_p.
            eta_n = eta;
            eta_p = eta;
            return arch::math::isfinite(eta);
        }

        int lo = -1;
        int hi = -1;
        double q_lo = -std::numeric_limits<double>::infinity();
        double q_hi = std::numeric_limits<double>::infinity();
        for (int i = 0; i < NUM_SPEC; ++i) {
            if (NetType::spin_weight(i) <= 0.0) continue;
            const double q = NetType::zion(i) / NetType::aion(i);
            if (q < Ye && q > q_lo) {
                lo = i;
                q_lo = q;
            }
            if (q > Ye && q < q_hi) {
                hi = i;
                q_hi = q;
            }
        }

        if (attempt == 1 && lo >= 0 && hi >= 0) {
            double x_lo = (q_hi - Ye) / (q_hi - q_lo);
            double x_hi = 1.0 - x_lo;
            // Keep both initial mixing weights away from logarithmic endpoints.
            // The 1e-8 band belongs to this chemical-potential seed only; it is
            // not a floor imposed on the final equilibrium abundances.
            x_lo = arch::nse_detail::clamp_by_value(
                x_lo, 1.0e-8, 1.0 - 1.0e-8);
            x_hi = 1.0 - x_lo;

            const double n_lo = NetType::aion(lo) - NetType::zion(lo);
            const double n_hi = NetType::aion(hi) - NetType::zion(hi);
            const double z_lo = NetType::zion(lo);
            const double z_hi = NetType::zion(hi);
            const double det = n_lo * z_hi - z_lo * n_hi;
            if (std::abs(det) > std::numeric_limits<double>::epsilon()) {
                const double rhs_lo = arch::nse_detail::logarithm(x_lo) - log_base[lo];
                const double rhs_hi = arch::nse_detail::logarithm(x_hi) - log_base[hi];
                eta_n = (rhs_lo * z_hi - z_lo * rhs_hi) / det;
                eta_p = (n_lo * rhs_hi - rhs_lo * n_hi) / det;
                return arch::math::isfinite(eta_n) && arch::math::isfinite(eta_p);
            }
        }

        // Last restart perturbs the chemical-potential difference in the
        // direction required by the target charge fraction.
        const double anchor_q = NetType::zion(anchor) / NetType::aion(anchor);
        const double charge_shift =
            arch::nse_detail::clamp_by_value(
                20.0 * (Ye - anchor_q), -4.0, 4.0);
        eta_n = eta - charge_shift;
        eta_p = eta + charge_shift;
        return arch::math::isfinite(eta_n) && arch::math::isfinite(eta_p);
    }

    ARCH_HOST_DEVICE static bool check_conservation(
        const std::array<double, NUM_SPEC>& x, double Ye)
    {
        arch::math::CompensatedSum mass;
        arch::math::CompensatedSum charge;
        for (int i = 0; i < NUM_SPEC; ++i) {
            if (!arch::math::isfinite(x[i]) || x[i] < 0.0) return false;
            mass.add(x[i]);
            charge.add(x[i] * (NetType::zion(i) / NetType::aion(i)));
        }
        return std::abs(mass.value() - 1.0) <= conservation_tol
            && std::abs(charge.value() - Ye) <= conservation_tol;
    }
};
