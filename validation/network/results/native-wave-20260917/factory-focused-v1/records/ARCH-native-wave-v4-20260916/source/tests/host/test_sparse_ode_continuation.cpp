/** Focused backend-independent sparse storage and ODE suspension contracts. */
#include "numerics/linalg/CsrPattern.h"
#include "numerics/linalg/DenseWrap.h"
#include "numerics/burnsolver/ode_be-nr.h"
#include "numerics/burnsolver/ode_bd.h"
#include "numerics/burnsolver/ode_ros4.h"
#include "numerics/burnsolver/BurnerHandle.h"
#include "physics/network/WeakTableView.h"
#include "../math/DenseLuCases.h"
#include "../math/NetworkDerivativeCases.h"
#include "../math/BurnThermalCases.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
void require(bool value, const char* reason)
{
    if (!value) throw std::runtime_error(reason);
}

struct TransferNetwork
{
    static constexpr int NUM_SPECIES = 2, ODE_NEQ = 3;
    static constexpr bool SUPPORTS_NSE = false;
    static constexpr double ENERGY_CONVERSION = 0.0;
    ARCH_HOST_DEVICE static double aion(int) { return 1.0; }
    ARCH_HOST_DEVICE static double energy_weight(int) { return 0.0; }
    ARCH_HOST_DEVICE static void eval_rhs(
        const double* x, double, double, double* rhs, double& enuc)
    {
        rhs[0] = -x[0]; rhs[1] = x[0]; enuc = 0.0;
    }
    template <class Matrix>
    ARCH_HOST_DEVICE static void eval_jacobian(
        const double*, double, double, Matrix& matrix, double* energy)
    {
        matrix.set(1, 1, -1.0); matrix.set(2, 1, 1.0);
        energy[0] = 0.0; energy[1] = 0.0;
    }
    ARCH_HOST_DEVICE static void eval_temperature_derivative(
        const double*, double, double, double* rhs, double& energy)
    {
        rhs[0] = rhs[1] = 0.0; energy = 0.0;
    }
};
struct ConstantCv
{
    ARCH_HOST_DEVICE double get_eta(double, double, const double*) const { return 0.0; }
    ARCH_HOST_DEVICE double get_cv(double, double, const double*) const { return 1.0; }
    ARCH_HOST_DEVICE double get_eint_from_T(double, double t, const double*) const { return t; }
};

struct CompositionEnergyFixture {
    static constexpr int NUM_SPECIES = 3;
    static constexpr double ENERGY_CONVERSION = 2.0;
    static double aion(int index) { return std::array{1.0, 2.0, 4.0}[index]; }
    static double energy_weight(int index) { return std::array{3.0, 5.0, 7.0}[index]; }
};

void composition_energy_contract()
{
    const double before[]{.25, .25, .5, 100.0};
    const double after[]{.5, .25, .25, 200.0};
    // Independent dyadic oracle: 2 * (3/4 - 7/16) = 5/8.
    require(OdeMath::integrated_composition_energy<CompositionEnergyFixture>(after, before) == .625,
            "composition energy integral changed");
    require(OdeMath::integrated_composition_energy<CompositionEnergyFixture>(before, after) == -.625,
            "composition energy sign changed");
    require(OdeMath::integrated_composition_energy<CompositionEnergyFixture>(before, before) == 0,
            "unchanged composition releases energy");
}

void matrix_contract()
{
    const double table[12]{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    const arch::network::WeakTableStorageView storage{table, 12};
    const arch::network::WeakValuesView view{storage.slice(0, 12), 2, 3, 2};
    require(view(2, 3, 2) == 11 && view(1, 2, 1) == 2,
            "borrowed weak data changed Fortran table layout");
    require(storage.slice(11, 2) == nullptr && storage.slice(13, 0) == nullptr
                && std::isnan(view(0, 1, 1)) && std::isnan(view(1, 4, 1)),
            "borrowed weak data accepted an out-of-bounds access");
    arch::linalg::CsrPatternBuilder builder(201);
    builder.set(1, 201, 0.0); // A structural zero remains present.
    builder.set(201, 1, 5.0);
    builder.set(201, 1, 7.0); // Duplicate writes do not duplicate storage.
    const auto pattern = builder.finish();
    require(pattern.column_indices.size() == 203, "CSR unexpectedly has dense storage");
    std::vector<double> values(pattern.column_indices.size());
    auto matrix = pattern.view<201>(values.data());
    static_assert(std::is_trivially_copyable_v<decltype(matrix)>);
    matrix.set(1, 201, 2.0); matrix.set(201, 1, 5.0);
    require(matrix(1, 201) == 2.0 && matrix(201, 1) == 5.0,
            "CSR orientation was transposed");
    matrix.form_shifted_identity(-0.5);
    require(matrix(1, 1) == 1.0 && matrix(201, 201) == 1.0,
            "CSR omitted a shifted identity diagonal");
    require(matrix(1, 201) == -1.0 && matrix(201, 1) == -2.5,
            "CSR shifted coefficients changed");
    std::array<double, 201> exact{}, rhs{};
    exact.fill(1.0); rhs.fill(1.0);
    rhs[0] = 0.0; rhs[200] = -1.5;
    require(matrix.solution_accurate(rhs.data(), exact.data()),
            "CSR backward-error gate rejected an exact solution");
    exact[3] += 1.0e-3;
    require(!matrix.solution_accurate(rhs.data(), exact.data()),
            "CSR backward-error gate accepted a wrong solution");
    exact[3] = std::numeric_limits<double>::quiet_NaN();
    require(!matrix.solution_accurate(rhs.data(), exact.data()),
            "CSR backward-error gate accepted a nonfinite solution");
    std::vector<double> other_values(values.size());
    auto other = pattern.view<201>(other_values.data());
    other.set_shifted_identity_from(matrix, 2.0);
    require(other(1, 1) == 3.0 && other(1, 201) == -2.0,
            "CSR matrix-to-system transform differs from dense semantics");
    other.set(2, 3, 0.0);
    require(!other.valid(), "Missing structural zero was silently dropped");
    other.zero();
    require(!other.valid(), "Numeric zero erased a structural failure");
}

void higher_order_estimate_contract()
{
    // An explicit alternative cost table makes the prospective higher order
    // cheaper. The production Roman table currently never takes this branch
    // for positive finite data; this is a helper regression, not a fake claim
    // of an affected production trajectory.
    const int sequence[3]{2, 6, 10};
    const double costs[3]{2.0, 8.0, 10.0};
    for (const double poison : {std::numeric_limits<double>::quiet_NaN(), -1.0e200, 1.0e200}) {
        const double factors[3]{poison, 2.0, poison};
        const double next = OdeMath::bd_recommend_macro_step<3>(
            1.0, factors, 1, sequence, costs, 0.1, 10.0);
        require(std::isfinite(next) && std::abs(next - 3.0) < 1.0e-14,
                "BD higher-order estimate read a poisoned unevaluated factor");
    }
}

template <template <class, class, class> class Solver, class Network = TransferNetwork>
void continuation_contract(bool fail_first)
{
    constexpr int extent = Network::ODE_NEQ;
    using Dense = Solver<Network, DenseMatrixData<extent>, DenseLUSolver>;
    using Sparse = Solver<Network, CsrMatrixView<extent>, void>;
    static_assert(std::is_trivially_copyable_v<typename Sparse::Continuation>);
    BurnConfig config{};
    config.nuclearDensMin = 0.0; config.nuclearTempMin = 0.0;
    config.smallt = 1.0; config.smallx = 1.0e-30;
    config.odeconfig.rtol = 1.0e-4; config.odeconfig.atol = 1.0e-8;
    config.odeconfig.max_substeps = 10000;
    const auto cfg = make_burn_config_view(config);
    const ConstantCv eos{};
    std::array<double, extent> reference{0.75, 0.25, 100.0};
    if constexpr (OdeMath::has_nonconservative_energy<Network>) reference.back() = -0.25;
    auto state = reference;
    const auto handle = BurnerHandle<ConstantCv>::bind<Dense>();
    require(handle.state_size() == extent, "erased CPU burner lost its exact packed state extent");
    double dt = 0.01;
    const auto report = Dense::integrate_report(reference.data(), 1.0, dt, eos, cfg, dt);
    require(report.success(), "Synchronous reference failed");
    auto erased_state = state;
    double erased_dt = 0.01, erased_energy = std::numeric_limits<double>::quiet_NaN();
    require(handle.integrate(erased_state.data(), 1.0, 0.01, eos, config, erased_dt, &erased_energy)
                && erased_state == reference && erased_dt == dt
                && erased_energy == report.energy_change,
            "CPU erased burner lost the accepted energy integral");

    arch::linalg::CsrPatternBuilder builder(extent);
    double unused[extent]{};
    Network::eval_jacobian(state.data(), 1.0, 0.0, builder, unused);
    OdeMath::include_burn_coupling<Network>(builder);
    const auto pattern = builder.finish();
    std::vector<double> values(pattern.column_indices.size());
    std::vector<double> jacobian_values(pattern.column_indices.size());
    auto matrix = pattern.template view<extent>(values.data());
    auto jacobian = pattern.template view<extent>(jacobian_values.data());
    typename Sparse::Continuation context;
    Sparse::begin(context, state.data(), 1.0, 0.01, cfg, 0.01);
    require(!Sparse::complete_linear_solve(context, true), "Unsolicited solve was consumed");
    int requests = 0;
    DenseMatrixData<extent> dense;
    int pivots[extent];
    for (;;) {
        const auto request = Sparse::advance(context, jacobian, matrix, state.data(), eos, cfg);
        if (request == OdeLinearRequest::Complete) break;
        require(matrix.valid(), "Network exceeded captured structural pattern");
        const auto before = state;
        require(Sparse::advance(context, jacobian, matrix, state.data(), eos, cfg)
                    == request && state == before,
                "Outstanding request advanced or committed without a response");
        // Test-only dense oracle consumes the SAME CSR values; no second ODE.
        if (request != OdeLinearRequest::SolveWithFactors)
            for (int row = 1; row <= extent; ++row)
                for (int column = 1; column <= extent; ++column)
                    dense.set(row, column, matrix(row, column));
        bool success = !(fail_first && requests == 0);
        if (success) {
            if (request == OdeLinearRequest::FactorizeAndSolve)
                success = DenseLUSolver::solve<extent, extent>(dense, context.b);
            else if (request == OdeLinearRequest::Factorize)
                success = DenseLUSolver::factorize<extent, extent>(dense, pivots);
            else
                DenseLUSolver::solve_with_factors<extent, extent>(dense, pivots, context.b);
        }
        require(Sparse::complete_linear_solve(context, success), "Solve response was lost");
        require(!Sparse::complete_linear_solve(context, success), "Solve response consumed twice");
        ++requests;
    }
    require(requests > 0 && context.report.success(), "Continuation did not integrate");
    require(context.report.rejected_substeps >= (fail_first ? 1 : 0),
            "Linear failure did not reject and retry the substep");
    require(state[0] < 0.75 && state[1] > 0.25, "Continuation is a no-op");
    if constexpr (OdeMath::has_nonconservative_energy<Network>) {
        require(state[2] < 100.0 && state.back() < -0.25,
                "CSR continuation lost cooling or clamped its signed source integral");
        require(std::abs(state[2] - 100.0 - (0.75 - state[0] + state.back() + 0.25)) < 1.e-12,
                "CSR continuation lost integrated energy closure");
    } else require(state[2] == 100.0, "Continuation changed zero-source thermal closure");
    require(std::abs(state[0] + state[1] - 1.0) < 1.0e-14,
            "Continuation lost mass conservation");
    if (!fail_first) {
        require(state == reference && dt == context.dt_recommended,
                "Suspension changed the shared synchronous algorithm");
        require(report.attempted_substeps == context.report.attempted_substeps
                    && report.rejected_substeps == context.report.rejected_substeps
                    && report.energy_change == context.report.energy_change,
                "Suspension changed retry accounting");
    }
}
} // namespace

int main()
{
    try {
        matrix_contract();
        require(DenseLuCases::mixed_units(), "Dense LU lost componentwise mixed-unit accuracy");
        require(DenseLuCases::failure_controls(), "Dense LU accepted a singular/nonfinite matrix");
        require(arch::test::network_derivative_contract(), "complete-RHS temperature derivative contract failed");
        require(arch::test::burn_thermal::jacobian_contract(), "thermal Jacobian chain rule failed");
        const auto thermal_config = arch::test::burn_thermal::convergence_config();
        require(arch::test::burn_thermal::composition_energy_control<Solver_BE_NR>(thermal_config)
                    && arch::test::burn_thermal::composition_energy_control<Solver_BD>(thermal_config)
                    && arch::test::burn_thermal::composition_energy_control<Solver_ROS4>(thermal_config),
                "accepted sub-ULP species transfer lost its signed binding energy");
        require(arch::test::burn_thermal::ros4_failure_control(thermal_config).success,
                "ROS4 factor/solve failures bypassed retry, stall or rollback handling");
        require(arch::test::burn_thermal::accepted_state_contract(),
                "accepted-state trial/projection arithmetic failed");
        require(arch::test::burn_thermal::translation_control<Solver_BE_NR>(thermal_config).success,
                "BE_NR constant heating/cooling lost a representable increment");
        require(arch::test::burn_thermal::translation_control<Solver_BE_NR>(thermal_config, 16).success,
                "BE_NR accepted substeps lost accumulated heating/cooling");
        require(arch::test::burn_thermal::translation_control<Solver_BD>(thermal_config).success,
                "BD constant heating/cooling lost a representable increment");
        require(arch::test::burn_thermal::translation_control<Solver_ROS4>(thermal_config).success,
                "ROS4 constant heating/cooling lost a representable increment");
        require(arch::test::burn_thermal::translation_control<Solver_BD>(thermal_config, 16).success,
                "BD accepted substeps lost accumulated heating/cooling");
        require(arch::test::burn_thermal::translation_control<Solver_ROS4>(thermal_config, 16).success,
                "ROS4 accepted substeps lost accumulated heating/cooling");
        require(arch::test::burn_thermal::bound_network_suite(thermal_config).success,
                "shared ODE lost or mixed explicit network data views");
        const auto external = arch::test::burn_thermal::external_energy_suite(thermal_config);
        require(external.jacobian,
                "signed energy quadrature is missing its RHS or full Jacobian");
        for (bool rollback : external.rollback)
            require(rollback, "rejected ODE solve committed a temperature/loss trial");
        for (const auto& method : external.paths)
            for (const auto& path : method)
                require(path.success, "ODE failed independent cooling/quadrature refinement");
        require(arch::test::burn_thermal::be_tolerance_control(thermal_config).success,
                "BE_NR tolerance controlled Newton convergence but not time integration");
        require(arch::test::burn_thermal::ros4_convergence(false, thermal_config).success,
                "ROS4 lost fourth-order temperature convergence");
        require(arch::test::burn_thermal::ros4_convergence(true, thermal_config).success,
                "ROS4 lost fourth-order composition-dependent cv convergence");
        composition_energy_contract();
        higher_order_estimate_contract();
        continuation_contract<Solver_BE_NR>(false);
        continuation_contract<Solver_BE_NR>(true);
        continuation_contract<Solver_BD>(false);
        continuation_contract<Solver_BD>(true);
        continuation_contract<Solver_ROS4>(false);
        continuation_contract<Solver_ROS4>(true);
        for (bool fail_first : {false, true}) {
            using External = arch::test::burn_thermal::ExternalEnergyReaction;
            continuation_contract<Solver_BE_NR, External>(fail_first);
            continuation_contract<Solver_BD, External>(fail_first);
            continuation_contract<Solver_ROS4, External>(fail_first);
        }
        std::cout << "Sparse matrix and all three shared ODE continuation contracts passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
