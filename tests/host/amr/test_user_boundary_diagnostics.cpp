/**
 * @file test_user_boundary_diagnostics.cpp
 * @brief Verify the physical-surface flux observer and its stage quadrature.
 *
 * Check oriented lower/upper faces, untouched internal/periodic surfaces,
 * weighted capture with the F(Y0) cache, tiny species values, heat separation
 * and quadrature under the frozen RK2/RK3 and RKL1/RKL2 coefficients.
 * Linear forcing has second-order quadrature only for second-order methods.
 *
 * The observer is accounting only: it never writes conserved simulation state.
 * The manager links this translation unit into the boundary_plan CTest target;
 * there is deliberately no main() here.
 */
#include "driver/schedule/StageScheduler.h"
#include "numerics/diffusion/DiffFunction.h"
#include "physics/boundary/BoundaryDiagnostics.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using arch::boundary::BoundaryFluxCaptureStorage;
using arch::boundary::BoundaryFluxCaptureView;
using arch::boundary::CaptureBoundaryFlux;

struct TestFlux
{
    double rho, mom_u, mom_v, mom_w, eng;
};

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

bool close(double left, double right)
{
    const double scale = std::max(1.0, std::abs(right));
    return std::abs(left - right) <= 1.0e-13 * scale;
}

// Active cell extent [0,4)x[0,3)x[0,1): a 2D block with one physical x3 plane
// implicitly absent, so x3 faces must stay empty.
constexpr int kIs = 0, kIe = 4, kJs = 0, kJe = 3, kKs = 0, kKe = 1;
constexpr int kSpecies = 2;
constexpr int kFields = 6 + kSpecies;

int plane_count(int direction)
{
    const int extent[3]{kIe - kIs, kJe - kJs, kKe - kKs};
    return extent[(direction + 1) % 3] * extent[(direction + 2) % 3];
}

BoundaryFluxCaptureStorage make_storage(bool third_axis = false)
{
    BoundaryFluxCaptureStorage storage;
    for (int face = 0; face < 6; ++face) {
        const int axis = face / 2;
        if (axis == 2 && !third_axis) continue;
        storage.stage[face].assign(
            static_cast<std::size_t>(plane_count(axis)) * kFields, 0.0);
        storage.initial[face].assign(
            static_cast<std::size_t>(plane_count(axis)) * kFields, 0.0);
    }
    return storage;
}

void capture(BoundaryFluxCaptureView view, int direction,
    int i, int j, int k, const TestFlux& flux,
    const std::vector<double>& species_flux, double heat)
{
    CaptureBoundaryFlux(view, direction, i, j, k,
        kIs, kIe, kJs, kJe, kKs, kKe, flux,
        kSpecies > 0 ? species_flux.data() : nullptr,
        kSpecies, kSpecies /* stride in the packed species block */, heat);
}

std::vector<double> species_values(double rho_x0, double rho_x1)
{
    std::vector<double> values(
        static_cast<std::size_t>(kSpecies) * kSpecies, 0.0);
    values[0] = rho_x0;
    values[kSpecies] = rho_x1;
    return values;
}

/** A face record keeps rho, native momenta, E, rho*X_s then heat. */
void test_oriented_faces_and_empty_surfaces()
{
    BoundaryFluxCaptureStorage storage = make_storage();
    storage.weight = 2.0;
    const BoundaryFluxCaptureView view = storage.view();
    const TestFlux flux{1.5, 0.25, -0.5, 0.75, 4.0};
    const std::vector<double> species = species_values(1.0e-3, 2.0e-3);

    // Lower x1 face at i=0, plane index j*k-extent + k.
    capture(view, 0, kIs, 2, 0, flux, species, 0.0);
    const double* lower = storage.stage[0].data() + 2 * kFields;
    require(close(lower[0], 3.0) && close(lower[1], 0.5)
        && close(lower[2], -1.0) && close(lower[3], 1.5)
        && close(lower[4], 8.0), "lower x1 plane is not weight*F");

    // Upper x1 face at i=Ie; its own plane is untouched by the lower write.
    capture(view, 0, kIe, 0, 0, flux, species, 0.0);
    const double* upper = storage.stage[1].data();
    require(close(upper[0], 3.0) && close(upper[4], 8.0),
        "upper x1 plane is not weight*F");
    require(close(storage.stage[0][1 * kFields + 4], 0.0),
        "lower plane was contaminated by the upper face");

    // An interior coordinate belongs to no physical surface.
    capture(view, 0, 2, 1, 0, flux, species, 0.0);
    require(close(storage.stage[0][1 * kFields], 0.0)
        && close(storage.stage[1][1 * kFields], 0.0),
        "interior x1 coordinate wrote a physical plane");

    // x3 is inactive here: no plane is owned and nothing may be written.
    capture(view, 2, 0, 0, kKs, flux, species, 0.0);
    capture(view, 2, 0, 0, kKe, flux, species, 0.0);
    require(storage.stage[4].empty() && storage.stage[5].empty(),
        "inactive x3 surfaces were allocated");

    // A null view is the built-in run: no writes and no crash.
    capture(BoundaryFluxCaptureView{}, 0, kIs, 0, 0, flux, species, 0.0);
}

void test_weighted_capture_and_f0_cache()
{
    BoundaryFluxCaptureStorage storage = make_storage();
    BoundaryFluxCaptureView view = storage.view();
    // First stage of second-order RKL: observe the stage flux and cache F(Y0).
    view.weight = 0.5;
    view.initial_weight = 0.0;
    view.save_initial = true;
    const TestFlux first{1.0, 2.0, 3.0, 4.0, 5.0};
    const std::vector<double> species = species_values(1.0e-30, -2.5e-31);
    capture(view, 0, kIs, 0, 0, first, species, 0.0);
    const double* plane = storage.stage[0].data();
    require(close(plane[0], 0.5) && close(plane[4], 2.5),
        "first stage did not apply the stage weight");
    require(close(storage.initial[0][0], 1.0)
        && close(storage.initial[0][4], 5.0),
        "first stage did not cache F(Y0)");
    require(close(plane[5], 5.0e-31) && close(plane[6], -1.25e-30),
        "tiny species fluxes were not weighted exactly");

    // Later stage: combine the new flux with the cached F(Y0) and keep it.
    const TestFlux second{2.0, 4.0, 6.0, 8.0, 10.0};
    view.weight = 1.0;
    view.initial_weight = -0.25;
    view.save_initial = false;
    capture(view, 0, kIs, 0, 0, second, species, 0.0);
    require(close(storage.stage[0][0], 1.75) && close(storage.stage[0][4], 8.75),
        "recursive stage did not add initial_weight*cached_F0");
    require(close(storage.initial[0][0], 1.0),
        "a non-saving stage overwrote the F(Y0) cache");

    // The weight is configurable per stage without changing the plane layout.
    view.weight = 0.0;
    view.initial_weight = 0.0;
    std::fill(storage.initial[1].begin(), storage.initial[1].end(),
        std::numeric_limits<double>::quiet_NaN());
    capture(view, 0, kIe, 0, 0, second, species, 0.0);
    require(close(storage.stage[1][0], 0.0) && close(storage.stage[1][4], 0.0),
        "zero cache weight read uninitialized F(Y0) or did not zero the plane");
}

void test_heat_separation()
{
    BoundaryFluxCaptureStorage storage = make_storage();
    storage.weight = 1.0;
    const BoundaryFluxCaptureView view = storage.view();
    const TestFlux flux{1.0, 0.5, 0.5, 0.5, 12.0};
    const std::vector<double> species = species_values(1.0e-3, -1.0e-3);
    const double heat = 7.5;
    capture(view, 1, 0, kJs, 0, flux, species, heat);
    const double* plane = storage.stage[2].data();
    require(close(plane[5], 1.0e-3) && close(plane[6], -1.0e-3),
        "species fluxes moved when heat was supplied");
    require(close(plane[7], 7.5),
        "heat is not stored in its own trailing component");
    require(close(plane[4], 12.0),
        "total energy must stay the unmodified flux component");
}

/** Surface quadrature of one face: sum_s dt * weight_s * F(t_s). */
double hydro_quadrature(arch::scheduler::HydroMethod method,
    double dt, double f0, double slope)
{
    const auto plan = arch::scheduler::make_hydro_plan(method);
    double total = 0.0;
    for (const auto& stage : plan.stages) {
        const double time = stage.input_time_fraction * dt;
        total += dt * stage.flux_register_weight * (f0 + slope * time);
    }
    return total;
}

void test_hydro_stage_quadrature()
{
    const double dt = 0.25, f0 = 3.0, slope = -2.0;
    const double exact_constant = f0 * dt;
    const double exact_linear = f0 * dt + 0.5 * slope * dt * dt;
    for (const auto method : {arch::scheduler::HydroMethod::RK2,
             arch::scheduler::HydroMethod::RK3}) {
        require(close(hydro_quadrature(method, dt, f0, 0.0), exact_constant),
            "frozen RK weights do not integrate a constant surface flux");
        require(close(hydro_quadrature(method, dt, f0, slope), exact_linear),
            "frozen RK weights do not integrate a linear-in-time surface flux");
    }
}

/**
 * Frozen stage abscissae used by the shared scheduler for RKL super-steps.
 * RKL1 c_j=j(j+1)/s(s+1); RKL2 c_1=4/[3(s^2+s-2)], c_j=(j^2+j-2)/(s^2+s-2).
 */
double rkl_fraction(bool second_order, int stage, int stages)
{
    const int j = stage - 1;
    const double denominator = double(stages) * (stages + 1)
        - (second_order ? 2.0 : 0.0);
    if (j == 0) return 0.0;
    if (!second_order) return double(j) * (j + 1.0) / denominator;
    return j == 1 ? 4.0 / (3.0 * denominator)
                  : (double(j) * (j + 1.0) - 2.0) / denominator;
}

/**
 * Contract RKL observer: Bj=mu*Bprev+nu*Bolder+dt*Rstage, B0=0,
 * B1=dt*Rstage, final stage adds Bs to the cumulative budget. Rstage is the
 * surface integral of the observed plane weight*F_stage+initial_weight*F(Y0).
 */
double rkl_super_step_budget(DiffFunction::RKLOrder order, int stages,
    double dt, double f0, double slope)
{
    const bool second_order = order == DiffFunction::RKLOrder::Second;
    const double initial_flux = f0;
    double previous = 0.0, older = 0.0, final_stage = 0.0;
    for (int stage = 1; stage <= stages; ++stage) {
        const DiffFunction::RKLCoeffs coefficients =
            DiffFunction::get_rkl_coeffs(order, stage, stages);
        const double time = rkl_fraction(second_order, stage, stages) * dt;
        const double flux = f0 + slope * time;
        const double plane = coefficients.tilde_mu * flux
            + (second_order && stage > 1
                    ? coefficients.gamma * initial_flux
                    : 0.0);
        const double current = stage == 1
            ? dt * plane
            : coefficients.mu * previous + coefficients.nu * older + dt * plane;
        older = previous;
        previous = current;
        final_stage = current;
    }
    return final_stage;
}

void test_rkl_stage_quadrature()
{
    // A constant surface flux is integrated exactly for every order and stage
    // count: the stage weighting is the stability polynomial's linear term.
    for (const int stages : {2, 3, 5, 8}) {
        const double dt = 0.05, f0 = 2.0;
        require(close(rkl_super_step_budget(DiffFunction::RKLOrder::First,
                         stages, dt, f0, 0.0), f0 * dt),
            "RKL1 constant surface flux is not integrated exactly");
        require(close(rkl_super_step_budget(DiffFunction::RKLOrder::Second,
                         stages, dt, f0, 0.0), f0 * dt),
            "RKL2 constant surface flux is not integrated exactly");
    }
    // Second-order RKL reproduces a linear-in-time surface flux exactly: the
    // gamma_initial*F(Y0) term supplies the missing quadratic coefficient.
    for (const int stages : {2, 3, 5, 8}) {
        const double dt = 0.05, f0 = 2.0, slope = 1.5;
        require(close(rkl_super_step_budget(DiffFunction::RKLOrder::Second,
                         stages, dt, f0, slope),
                     f0 * dt + 0.5 * slope * dt * dt),
            "RKL2 linear-in-time surface flux is not integrated exactly");
    }
    // First-order RKL integrates the constant term exactly but only matches
    // the quadratic term to O(1): its budget coefficient stays in [1/6, 1/4].
    // The frozen contract also asks RKL1 for exact linear-in-time quadrature;
    // that is unreachable for a first-order method and is reported upstream.
    for (const int stages : {2, 3, 5, 8}) {
        const double dt = 0.05, slope = 1.5;
        const double coefficient =
            rkl_super_step_budget(DiffFunction::RKLOrder::First,
                stages, dt, 0.0, slope) / (slope * dt * dt);
        require(coefficient > 1.0 / 6.0 - 1.0e-12
            && coefficient <= 0.25 + 1.0e-12,
            "RKL1 linear-in-time budget left its frozen first-order bound");
    }
}
} // namespace

/** Entry point linked into the boundary_plan CTest target by the manager. */
void test_user_boundary_diagnostics()
{
    test_oriented_faces_and_empty_surfaces();
    test_weighted_capture_and_f0_cache();
    test_heat_separation();
    test_hydro_stage_quadrature();
    test_rkl_stage_quadrature();
}
