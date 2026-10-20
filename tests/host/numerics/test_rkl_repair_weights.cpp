/**
 * @file test_rkl_repair_weights.cpp
 * @brief Impulse regression for DiffFunction::repair_weights (contract RKL-weights-1).
 *
 * Independent reference: replay the forward global-mass recurrence with zero initial
 * mass and a unit impulse injected at one stage; for a closed conservative operator the
 * final mass equals the returned gain for that stage. Coefficients are taken from
 * get_rkl_coeffs, the same production source the helper uses, so the test validates the
 * adjoint indexing and recurrence rather than re-deriving the RKL polynomials.
 *
 * Scope limits: this pulse test proves the global final-mass correction (signed weights)
 * only. It does not prove the spatial response of the diffusion operator, positivity of
 * the repaired trajectory, or behaviour with open-boundary flux, which the stage
 * controllers must account for separately.
 */
#include "numerics/diffusion/DiffFunction.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
    // Runtime-fed parameters keep the optimizer from collapsing these loops into
    // compile-time constants, so the checks stay live on the compiled sources.
    volatile int g_stage_limit = 256;
    volatile double g_zero_mass = 0.0;

    int stage_limit() { return g_stage_limit; }
    double zero_mass() { return g_zero_mass; }

    // Scale-aware tolerance for the pulse comparison. The reference recurrence applies
    // one stage update per step, each bounded by
    //     |mu_j| * |M_(j-1)| + |nu_j| * |M_(j-2)| + |1 - mu_j - nu_j| * |M_0| + |delta_j|,
    // with |mu_j| <= 3 and |nu_j| <= 3 for both RKL1 and RKL2. Rounding therefore stays
    // within ~8 * eps per stage relative to the running magnitude, which is bounded by
    // max(|weights|, 1) (impulses of unit size, zero initial mass). The factor 64 leaves
    // headroom for the rounding of the b_j-ratio coefficient evaluation shared by the
    // reference and the helper, and `impulses` widens the bound for the combined check.
    double pulse_tolerance(int stages, double max_abs_weight, int impulses)
    {
        const double eps = std::numeric_limits<double>::epsilon();
        const double per_stage = 64.0 * eps * static_cast<double>(stages) * static_cast<double>(impulses);
        return per_stage * std::max(1.0, max_abs_weight);
    }

    // Forward global-mass recurrence with zero initial mass:
    //     M_j = mu_j * M_(j-1) + nu_j * M_(j-2) + (1 - mu_j - nu_j) * M_0 + delta_j.
    // impulses[k - 1] is the impulse injected at stage k (1-based); the returned value is
    // the final global mass M_s.
    double forward_final_mass(DiffFunction::RKLOrder order, int stages, const std::vector<double>& impulses)
    {
        const double m0 = zero_mass();
        double m_j_minus_2 = 0.0;
        double m_j_minus_1 = 0.0;
        double m_j = 0.0;
        for (int j = 1; j <= stages; ++j) {
            const DiffFunction::RKLCoeffs c = DiffFunction::get_rkl_coeffs(order, j, stages);
            m_j = c.mu * m_j_minus_1 + c.nu * m_j_minus_2 +
                  (1.0 - c.mu - c.nu) * m0 + impulses[static_cast<std::size_t>(j - 1)];
            m_j_minus_2 = m_j_minus_1;
            m_j_minus_1 = m_j;
        }
        return m_j;
    }

    const char* order_name(DiffFunction::RKLOrder order)
    {
        return order == DiffFunction::RKLOrder::First ? "RKL1" : "RKL2";
    }

    int fail(const char* what, DiffFunction::RKLOrder order, int stages, int stage, double expected, double actual)
    {
        std::cerr << std::setprecision(17) << what << " order=" << order_name(order)
                  << " stages=" << stages << " stage=" << stage
                  << " expected=" << expected << " actual=" << actual << '\n';
        return 1;
    }

    int check_rejects_nonpositive(DiffFunction::RKLOrder order)
    {
        // Stages are 1-based and validated up front; nonpositive input is a programming
        // error and must surface as std::invalid_argument rather than a silent empty vector.
        const int bad_stages[] = {0, -1, -7, std::numeric_limits<int>::min()};
        for (int stages : bad_stages) {
            bool rejected = false;
            try {
                const std::vector<double> weights = DiffFunction::repair_weights(order, stages);
                (void)weights;
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            if (!rejected) {
                std::cerr << "repair_weights accepted nonpositive stages=" << stages
                          << " order=" << order_name(order) << '\n';
                return 1;
            }
        }
        return 0;
    }

    // Per-stage unit impulses: insert one impulse into the forward recurrence and compare
    // the final mass against the returned gain for that stage.
    int check_unit_impulses(DiffFunction::RKLOrder order, int stages)
    {
        const std::vector<double> weights = DiffFunction::repair_weights(order, stages);
        if (weights.size() != static_cast<std::size_t>(stages)) {
            std::cerr << "repair_weights size mismatch: order=" << order_name(order)
                      << " stages=" << stages << " size=" << weights.size() << '\n';
            return 1;
        }

        double max_abs_weight = 0.0;
        for (double w : weights) {
            if (!std::isfinite(w)) {
                std::cerr << "repair_weights produced a non-finite gain: order=" << order_name(order)
                          << " stages=" << stages << " value=" << w << '\n';
                return 1;
            }
            max_abs_weight = std::max(max_abs_weight, std::fabs(w));
        }

        // w_s == 1 exactly for every stage count, and the documented s == 1 case is {1}.
        if (weights[static_cast<std::size_t>(stages - 1)] != 1.0) {
            return fail("final gain is not 1", order, stages, stages,
                        1.0, weights[static_cast<std::size_t>(stages - 1)]);
        }
        if (stages == 1 && weights[0] != 1.0) {
            return fail("stage-1 special case is not {1}", order, stages, 1, 1.0, weights[0]);
        }

        const double tol = pulse_tolerance(stages, max_abs_weight, 1);
        std::vector<double> impulses(static_cast<std::size_t>(stages), 0.0);
        for (int k = 1; k <= stages; ++k) {
            impulses[static_cast<std::size_t>(k - 1)] = 1.0;
            const double final_mass = forward_final_mass(order, stages, impulses);
            impulses[static_cast<std::size_t>(k - 1)] = 0.0;
            const double expected = weights[static_cast<std::size_t>(k - 1)];
            if (std::fabs(final_mass - expected) > tol) {
                return fail("unit impulse mismatch", order, stages, k, expected, final_mass);
            }
        }
        return 0;
    }

    // Independent combined signed-impulse check: a single forward run with several signed
    // impulses must reproduce the linear superposition of the corresponding gains. This
    // exercises cross-stage cancellation that the per-stage sweep cannot see.
    int check_combined_signed_impulses(DiffFunction::RKLOrder order, int stages)
    {
        const std::vector<double> weights = DiffFunction::repair_weights(order, stages);
        if (weights.size() != static_cast<std::size_t>(stages)) {
            std::cerr << "repair_weights size mismatch: order=" << order_name(order)
                      << " stages=" << stages << " size=" << weights.size() << '\n';
            return 1;
        }

        // Deterministic mixed-sign impulses spread over the stage range: stage 1 always
        // receives +1 and every third stage alternates between +1 and -1, so larger stage
        // counts exercise genuine cross-stage cancellation.
        std::vector<double> impulses(static_cast<std::size_t>(stages), 0.0);
        double max_abs_weight = 0.0;
        int impulses_used = 0;
        for (int k = 1; k <= stages; ++k) {
            if (k == 1) {
                impulses[0] = 1.0;
                ++impulses_used;
            } else if ((k % 3) == 0) {
                impulses[static_cast<std::size_t>(k - 1)] = ((k / 3) % 2 == 0) ? 1.0 : -1.0;
                ++impulses_used;
            }
        }

        double superposed = 0.0;
        for (int k = 1; k <= stages; ++k) {
            const double sign = impulses[static_cast<std::size_t>(k - 1)];
            if (sign != 0.0) {
                superposed += sign * weights[static_cast<std::size_t>(k - 1)];
            }
            max_abs_weight = std::max(max_abs_weight, std::fabs(weights[static_cast<std::size_t>(k - 1)]));
        }

        const double final_mass = forward_final_mass(order, stages, impulses);
        const double tol = pulse_tolerance(stages, max_abs_weight, impulses_used);
        if (std::fabs(final_mass - superposed) > tol) {
            std::cerr << std::setprecision(17)
                      << "combined signed-impulse mismatch: order=" << order_name(order)
                      << " stages=" << stages << " impulses=" << impulses_used
                      << " superposed=" << superposed << " final_mass=" << final_mass
                      << " tol=" << tol << '\n';
            return 1;
        }
        return 0;
    }
}

int main()
{
    const DiffFunction::RKLOrder orders[] = {DiffFunction::RKLOrder::First, DiffFunction::RKLOrder::Second};
    const int max_stages = stage_limit();
    double checksum = 0.0;

    for (DiffFunction::RKLOrder order : orders) {
        const int rejected = check_rejects_nonpositive(order);
        if (rejected != 0) return rejected;

        // Contract bound: every possible impulse stage for total stages 1..256.
        for (int stages = 1; stages <= max_stages; ++stages) {
            const int rc = check_unit_impulses(order, stages);
            if (rc != 0) return rc;

            // Stride the combined check to keep the runtime bounded; the selected stage
            // counts still cover the small, mid and maximal ranges.
            if (stages <= 32 || stages % 17 == 0 || stages == max_stages) {
                const int combined = check_combined_signed_impulses(order, stages);
                if (combined != 0) return combined;
                const std::vector<double> weights = DiffFunction::repair_weights(order, stages);
                checksum += weights[0] + weights[static_cast<std::size_t>(stages - 1)];
            }
        }
    }

    std::cout << "RKL_REPAIR_WEIGHTS_PASS checksum=" << std::setprecision(17) << checksum << '\n';
    return 0;
}
