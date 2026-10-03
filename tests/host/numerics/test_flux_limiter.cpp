/**
 * @file test_flux_limiter.cpp
 * @brief Common flux constraints, zero-trace continuity and conservative mixing.
 *
 * Workflow:
 * 1. Independently construct the Lax-Friedrichs flux and its fluid/species bar states.
 * 2. Recover theta from the emitted flux and check Q_s +/- theta*D_s >= 0,
 *    strict fluid admissibility and sum(species flux) == mass flux.
 * 3. Exercise the finite-switch witness, both signs of real violations,
 *    physical face reflection and density/energy scaling through 1e-100.
 *
 * The shared 64-epsilon trace band is unchanged. Reflections exchange the cells
 * and reverse normal velocity, rather than merely swapping the input states.
 */
#include "physics/eos/IdealGas.h"
#include "numerics/flux/FluxHLLC.h"
#include "numerics/flux/InvariantDomainFlux.h"
#include "numerics/state/StateAdmissibility.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>

namespace {

constexpr int kDim = 0;      // x-direction face
constexpr int kSpecies = 2;
// Frozen shared trace band, 64*double epsilon. The leaf asserts it was not
// retuned; it is used below as the only slack for exact algebraic identities.
constexpr double kBand = arch::state::composition_roundoff_limit;
const IdealGasView kEos{};   // gamma = 1.4, composition-independent

int checks = 0;
int failures = 0;

void check(bool ok, const char* what)
{
    ++checks;
    if (!ok) {
        std::cerr << "FAIL " << what << '\n';
        ++failures;
    }
}

FluidVector conserved(double rho, double u, double p)
{ return {rho, rho * u, 0.0, 0.0, p / 0.4 + 0.5 * rho * u * u}; }

double max_abs_diff(const FluidVector& a, const FluidVector& b)
{
    const double d[5] = {a.rho - b.rho, a.mom_u - b.mom_u, a.mom_v - b.mom_v,
                         a.mom_w - b.mom_w, a.eng - b.eng};
    double m = 0.0;
    for (double v : d) m = std::max(m, std::abs(v));
    return m;
}

bool finite_state(const FluidVector& a)
{
    return std::isfinite(a.rho) && std::isfinite(a.mom_u) && std::isfinite(a.mom_v)
        && std::isfinite(a.mom_w) && std::isfinite(a.eng);
}

// Mirror of an emitted conservative state across the face plane (x -> -x):
// density, internal energy and tangential momentum are kept, the normal
// momentum component is negated.
FluidVector reflect_state(const FluidVector& u)
{ return {u.rho, -u.mom_u, u.mom_v, u.mom_w, u.eng}; }

// The same mirror on a normal-direction flux: mass, energy and tangential
// momentum flux flip sign, the normal-momentum flux is preserved.
FluidVector reflect_flux(const FluidVector& f)
{ return {-f.rho, f.mom_u, -f.mom_v, -f.mom_w, -f.eng}; }

// One 64eps band relative to the magnitudes under comparison. Deliberately the
// frozen trace band, never a new tolerance knob.
double band_of(double a, double b)
{ return kBand * std::max(std::abs(a), std::abs(b)); }

bool close_rel(double actual, double expected)
{ return std::abs(actual - expected) <= band_of(actual, expected); }

// Value inside the closed span of two emitted values, up to the frozen band.
bool in_span(double low, double value, double high)
{
    const double band = kBand * std::max(std::abs(low), std::max(std::abs(high), std::abs(value)));
    return value >= std::min(low, high) - band && value <= std::max(low, high) + band;
}

// Independently recomputed face quantities of the frozen formula.
struct Face {
    double a = 0.0;
    FluidVector fl{}, fr{}, low{}, bar{};
    double low_species[kSpecies]{}, bar_species[kSpecies]{};
    double Qbar[kSpecies]{}, D[kSpecies]{};
};

Face prepare(const FluidVector& L, const FluidVector& R, const double* xL,
             const double* xR, const FluidVector& high, const double* species_flux)
{
    Face f;
    double pL = 0.0, cL = 0.0, pR = 0.0, cR = 0.0;
    FluxAdmissibility::required_mean_thermo(L, xL, kEos, pL, cL);
    FluxAdmissibility::required_mean_thermo(R, xR, kEos, pR, cR);
    f.a = std::max(std::abs(get_un(L, kDim)) + cL, std::abs(get_un(R, kDim)) + cR);
    f.fl = get_flux(L, pL, kDim);
    f.fr = get_flux(R, pR, kDim);
    f.low = 0.5 * f.fl + 0.5 * f.fr - (0.5 * f.a) * (R - L);
    f.bar = 0.5 * L + 0.5 * R - (0.5 / f.a) * (f.fr - f.fl);
    const double c_rho = (f.low.rho - high.rho) / f.a;
    for (int s = 0; s < kSpecies; ++s) {
        const double ql = L.rho * xL[s], qr = R.rho * xR[s];
        const double fl_s = f.fl.rho * xL[s], fr_s = f.fr.rho * xR[s];
        f.low_species[s] = 0.5 * fl_s + 0.5 * fr_s - 0.5 * f.a * (qr - ql);
        f.bar_species[s] = 0.5 * ql + 0.5 * qr - (0.5 / f.a) * (fr_s - fl_s);
        f.Qbar[s] = f.bar_species[s] + kBand * f.bar.rho;
        f.D[s] = (f.low_species[s] - species_flux[s]) / f.a + kBand * c_rho;
    }
    return f;
}

FluidVector limit(const FluidVector& L, const FluidVector& R, const double* xL,
                  const double* xR, const FluidVector& high,
                  const double* species_flux, double* species_out)
{
    FluidVector limited = high;
    for (int s = 0; s < kSpecies; ++s) species_out[s] = species_flux[s];
    FluxAdmissibility::limit_face(L, R, xL, xR, kSpecies, kEos, kDim, limited, species_out);
    return limited;
}

// Recovered shared theta from the emitted species split of the zero-mean-trace
// cases, where low_species[s] is exactly zero.
double theta_from_species(const Face& f, int s, double species_high, double species_limited)
{
    const double denominator = species_high - f.low_species[s];
    return denominator != 0.0 ? (species_limited - f.low_species[s]) / denominator
                              : std::numeric_limits<double>::quiet_NaN();
}

// Independent shifted-cone inequality: Qbar_s +/- theta*D_s >= 0, with only the
// frozen 64eps band as slack. theta is the value recovered from the output.
bool cone_holds(const Face& f, int s, double theta)
{
    const double d = theta * f.D[s];
    const double band = kBand * std::max(std::abs(f.Qbar[s]), std::abs(d));
    return f.Qbar[s] + d >= -band && f.Qbar[s] - d >= -band;
}

// Retired zero-trace switch, reconstructed only to prove the leaf rejects it.
// This is not called by production; the assertions compare the measured output
// against this prediction.
struct RetiredPrediction {
    FluidVector flux;
    double theta = 0.0;
};

RetiredPrediction retired_zero_trace_switch(const Face& f, const FluidVector& high,
                                            const double* species_flux, double euler_theta)
{
    double theta = euler_theta;
    for (int s = 0; s < kSpecies; ++s) {
        const double deviation = std::abs((f.low_species[s] - species_flux[s]) / f.a);
        if (deviation > f.bar_species[s])
            theta = std::min(theta, std::max(0.0, f.bar_species[s]) / deviation);
    }
    RetiredPrediction p;
    p.theta = theta;
    p.flux = theta == 0.0 ? f.low : f.low + theta * (high - f.low);
    return p;
}

// Euler segment bound of the frozen density/internal-energy branch, evaluated
// with the shared recover()-based leaf (not the function under test).
double euler_theta_reference(const Face& f, const FluidVector& high)
{
    const FluidVector correction = (f.low - high) / f.a;
    return std::min(FluxAdmissibility::segment_fraction(f.bar, correction),
                    FluxAdmissibility::segment_fraction(f.bar, -1.0 * correction));
}

// Strict density/internal-energy check of the fluid blend, independent of
// segment_fraction: both bar-side states of the applied theta must recover.
void check_fluid_blend(const Face& f, const FluidVector& high, double theta,
                       const char* what)
{
    const FluidVector correction = (f.low - high) / f.a;
    check(FluxAdmissibility::valid(f.bar + theta * correction),
          what /* bar + theta*C admissible */);
    check(FluxAdmissibility::valid(f.bar - theta * correction),
          what /* bar - theta*C admissible */);
}

const char* kConePlus = "shifted cone bar + theta*C";

} // namespace

int main()
{
    check(kBand == 64.0 * std::numeric_limits<double>::epsilon(),
          "production trace band is still 64*double epsilon");

    // Valid ideal-gas means with exactly zero trace fraction in both cells.
    const FluidVector mean_L = conserved(1.0, 0.3, 1.0);
    const FluidVector mean_R = conserved(0.125, -0.1, 0.1);
    const double xm_L[kSpecies] = {1.0, 0.0};
    const double xm_R[kSpecies] = {1.0, 0.0};
    check(FluxAdmissibility::valid(mean_L) && FluxAdmissibility::valid(mean_R),
          "cell means are admissible ideal-gas states");

    bool ok = failures == 0;

    // ---- Case A: zero/tiny reconstructed trace over zero mean trace. --------
    const double traces[] = {0.0, 1e-16, 1e-30, 1e-100, 1e-189,
                             std::numeric_limits<double>::denorm_min()};
    FluidVector high_first;
    for (double e : traces) {
        const double xr_L[kSpecies] = {1.0 - e, e};
        const double xr_R[kSpecies] = {1.0, 0.0};
        FluidVector high;
        double species_high[kSpecies] = {0.0, 0.0};
        FluxHLLC<PCMReconstruction>::compute_face_flux(mean_L, mean_R, xr_L, xr_R,
            kSpecies, kEos, kDim, 0.0, high, species_high);
        check(finite_state(high) && std::isfinite(species_high[0])
              && std::isfinite(species_high[1]), "HLLC high-order flux is finite");
        // Normalized species flux sums: the split is consistent with the flux.
        check(close_rel(species_high[0] + species_high[1], high.rho),
              "input species flux sums to the high-order mass flux");

        const Face f = prepare(mean_L, mean_R, xm_L, xm_R, high, species_high);
        check(FluxAdmissibility::valid(f.bar), "strict fluid bar state is admissible");
        check(FluxAdmissibility::valid(f.low), "Lax-Friedrichs low face is admissible");
        check(f.bar_species[1] == 0.0, "cell means carry exactly zero trace");
        const double euler_theta = euler_theta_reference(f, high);

        double species_limited[kSpecies] = {0.0, 0.0};
        const FluidVector limited = limit(mean_L, mean_R, xm_L, xm_R, high,
                                          species_high, species_limited);
        check(finite_state(limited), "limited flux is finite");
        check(close_rel(species_limited[0] + species_limited[1], limited.rho),
              "limited species flux sums to the limited mass flux");

        // Hydrodynamic continuity: an at-most-tiny trace must not touch the
        // shared fluid flux or the species split.
        check(max_abs_diff(limited, high) == 0.0,
              "zero/tiny trace leaves the shared fluid flux unchanged");
        check(species_limited[0] == species_high[0]
              && species_limited[1] == species_high[1],
              "zero/tiny trace leaves the species split unchanged");
        if (species_high[1] != 0.0) {
            const double theta = theta_from_species(f, 1, species_high[1], species_limited[1]);
            check(theta == 1.0, "recovered theta is exactly one for the tiny trace");
            check(cone_holds(f, 0, theta) && cone_holds(f, 1, theta),
                  "tiny trace stays inside the shifted cone");
        }

        const RetiredPrediction retired =
            retired_zero_trace_switch(f, high, species_high, euler_theta);
        if (e > 0.0 && species_high[1] != 0.0) {
            // The old 0/0 branch maps any nonzero reconstructed trace to
            // theta = 0, i.e. a finite switch onto the Lax-Friedrichs face.
            check(retired.theta == 0.0 && max_abs_diff(retired.flux, high) > 0.1,
                  "retired zero-trace branch would switch the hydrodynamic flux");
            check(max_abs_diff(retired.flux, limited) > 0.1,
                  "measured limiter output rejects the retired switch");
        } else {
            check(retired.theta == euler_theta,
                  "exact zero trace has no switch in either formula");
        }

        if (e == 0.0) {
            high_first = high;
        } else {
            // Continuity across arbitrarily small traces: the hydrodynamic face
            // flux is unchanged relative to the exact-zero-trace row.
            check(close_rel(high.rho, high_first.rho)
                  && max_abs_diff(high, high_first) == 0.0,
                  "face hydrodynamic flux is independent of the tiny trace");
        }

        std::printf("A trace=%-10.3g high.rho=%-22.16g lim.rho=%-22.16g |lim-high|=%-8.3g "
                    "retired.rho=%-22.16g |retired-high|=%-8.3g spec1.high=%-10.3g\n",
                    e, high.rho, limited.rho, max_abs_diff(limited, high),
                    retired.flux.rho, max_abs_diff(retired.flux, high), species_high[1]);
    }

    // ---- Case B: true above-band violations stay limited, both signs. -------
    FluidVector high_B;
    double species_ref[kSpecies] = {0.0, 0.0};
    {
        const double xr_L[kSpecies] = {1.0 - 1e-3, 1e-3};
        const double xr_R[kSpecies] = {1.0, 0.0};
        FluxHLLC<PCMReconstruction>::compute_face_flux(mean_L, mean_R, xr_L, xr_R,
            kSpecies, kEos, kDim, 0.0, high_B, species_ref);
    }
    const double trace_violation = 0.5 * high_B.rho;   // far above the 64eps band
    double theta_base = 0.0;
    FluidVector limited_base;
    for (int sign = 0; sign < 2; ++sign) {
        const double trace = sign == 0 ? -trace_violation : trace_violation;
        double species_high[kSpecies] = {high_B.rho - trace, trace};
        check(close_rel(species_high[0] + species_high[1], high_B.rho),
              "violating input species flux sums to the mass flux");
        const Face f = prepare(mean_L, mean_R, xm_L, xm_R, high_B, species_high);
        check(FluxAdmissibility::valid(f.bar) && FluxAdmissibility::valid(f.low),
              "strict fluid bar and low face stay admissible");
        // Structural proof that this case is genuinely outside the band.
        check(std::abs(f.D[1]) > f.Qbar[1],
              "species trace deviation exceeds the shifted cone");
        const double euler_theta = euler_theta_reference(f, high_B);
        check(euler_theta == 1.0, "Euler branch alone would keep the high face");

        double species_limited[kSpecies] = {0.0, 0.0};
        const FluidVector limited = limit(mean_L, mean_R, xm_L, xm_R, high_B,
                                          species_high, species_limited);
        check(finite_state(limited), "limited above-band flux is finite");
        const double theta = theta_from_species(f, 1, species_high[1], species_limited[1]);
        check(theta > 0.0 && theta < 0.5,
              "true above-band violation is limited, not ignored or collapsed");
        check(theta < euler_theta, "species cone binds below the Euler-only theta");
        check(cone_holds(f, 0, theta) && cone_holds(f, 1, theta), kConePlus);
        check_fluid_blend(f, high_B, theta, "above-band fluid blend");
        check(close_rel(species_limited[0] + species_limited[1], limited.rho),
              "above-band species flux sums to the limited mass flux");
        check(in_span(f.low.rho, limited.rho, high_B.rho)
              && in_span(f.low.eng, limited.eng, high_B.eng)
              && in_span(f.low.mom_u, limited.mom_u, high_B.mom_u),
              "limited fluid flux is a convex blend of low and high");
        // The bounded face trace itself, with no new floor: it must be far
        // smaller than the violating input and still finite.
        check(std::abs(species_limited[1]) < std::abs(trace)
              && std::isfinite(species_limited[1]),
              "bounded trace magnitude is below the violating input");

        if (sign == 0) {
            theta_base = theta;
            limited_base = limited;
        }
        std::printf("B sign=%+d theta=%-12.6g euler=%-5.3g Qbar1=%-12.5g D1=%-12.5g "
                    "spec1.lim=%-12.5g lim.rho=%-22.16g\n",
                    sign == 0 ? -1 : 1, theta, euler_theta, f.Qbar[1], f.D[1],
                    species_limited[1], limited.rho);
    }

    // Physical reflection swaps the cells and reverses normal velocity.
    // Normal-momentum flux is even; mass, energy and species flux are odd.
    {
        const double species_high[kSpecies] = {high_B.rho + trace_violation, -trace_violation};
        const Face forward = prepare(mean_L, mean_R, xm_L, xm_R, high_B, species_high);
        const auto reflected_left = reflect_state(mean_R);
        const auto reflected_right = reflect_state(mean_L);
        const auto reflected_high = reflect_flux(high_B);
        const double reflected_species[kSpecies]{-species_high[0], -species_high[1]};
        const Face reverse = prepare(reflected_left, reflected_right, xm_R, xm_L,
                                     reflected_high, reflected_species);
        check(close_rel(forward.bar.rho, reverse.bar.rho)
              && close_rel(forward.bar.eng, reverse.bar.eng)
              && close_rel(forward.bar.mom_u, -reverse.bar.mom_u),
              "physical reflection transforms the fluid bar state");
        check(close_rel(forward.low.rho, -reverse.low.rho)
              && close_rel(forward.low.eng, -reverse.low.eng)
              && close_rel(forward.low.mom_u, reverse.low.mom_u),
              "physical reflection transforms the low fluid flux");
        for (int s = 0; s < kSpecies; ++s) {
            check(close_rel(forward.bar_species[s], reverse.bar_species[s])
                  && close_rel(forward.low_species[s], -reverse.low_species[s]),
                  "physical reflection transforms the species bar and low flux");
            check(close_rel(forward.Qbar[s], reverse.Qbar[s])
                  && close_rel(forward.D[s], -reverse.D[s]),
                  "physical reflection preserves Qbar and reverses D");
        }
        double reflected_limited_species[kSpecies];
        const auto reflected_limited = limit(reflected_left, reflected_right, xm_R, xm_L,
            reflected_high, reflected_species, reflected_limited_species);
        const double reflected_theta = theta_from_species(reverse, 1,
            reflected_species[1], reflected_limited_species[1]);
        check(close_rel(reflected_theta, theta_base),
              "physical reflection preserves the applied theta");
        check(cone_holds(reverse, 0, reflected_theta)
              && cone_holds(reverse, 1, reflected_theta),
              "reflected emitted flux satisfies both cone constraints");
        check(close_rel(reflected_limited.rho, -limited_base.rho)
              && close_rel(reflected_limited.eng, -limited_base.eng)
              && close_rel(reflected_limited.mom_u, limited_base.mom_u),
              "physical reflection transforms the emitted fluid flux");
        check(close_rel(reflected_limited_species[0] + reflected_limited_species[1],
                        reflected_limited.rho),
              "reflected species flux sums to the reflected mass flux");
        check_fluid_blend(reverse, reflected_high, reflected_theta, "reflected fluid blend");
    }

    // ---- Case D: scaling from 1 down to 1e-100. -----------------------------
    {
        const double scales[] = {1.0, 1e-12, 1e-30, 1e-60, 1e-100};
        const double species_base[kSpecies] = {high_B.rho + trace_violation, -trace_violation};
        for (double scale : scales) {
            const FluidVector L = scale * mean_L, R = scale * mean_R;
            const FluidVector high = scale * high_B;
            double species_high[kSpecies] = {scale * species_base[0], scale * species_base[1]};
            const Face f = prepare(L, R, xm_L, xm_R, high, species_high);
            check(FluxAdmissibility::valid(L) && FluxAdmissibility::valid(R)
                  && FluxAdmissibility::valid(f.bar),
                  "scaled states and bar remain admissible");
            double species_limited[kSpecies] = {0.0, 0.0};
            const FluidVector limited = limit(L, R, xm_L, xm_R, high, species_high,
                                              species_limited);
            const double theta = theta_from_species(f, 1, species_high[1], species_limited[1]);
            check(close_rel(theta, theta_base),
                  "scaling keeps the applied theta");
            check(close_rel(limited.rho, scale * limited_base.rho)
                  && close_rel(limited.eng, scale * limited_base.eng)
                  && close_rel(limited.mom_u, scale * limited_base.mom_u),
                  "scaling scales the limited flux linearly");
            check(cone_holds(f, 0, theta) && cone_holds(f, 1, theta),
                  "scaled case stays inside the shifted cone");
            check(close_rel(species_limited[0] + species_limited[1], limited.rho),
                  "scaled species flux sums to the limited mass flux");
            std::printf("D scale=%-8.3g theta=%-12.6g lim.rho=%-22.16g "
                        "scale*base.rho=%-22.16g\n",
                        scale, theta, limited.rho, scale * limited_base.rho);
        }
    }

    ok = failures == 0;
    std::printf("checks=%d failures=%d result=%s\n", checks, failures,
                ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
