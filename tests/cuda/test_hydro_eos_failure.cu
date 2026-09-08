/** Focused device-side EOS failure semantics; no full backend instantiation. */
#include "cuda/hydro/CheckedHydroEos.cuh"
#include "cuda/hydro/HydroReconstructionPolicies.cuh"
#include "cuda/hydro/HydroFluxPolicies.cuh"
#include "cuda/hydro/HydroStateKernels.cuh"
#include "numerics/integrator/GeometricSources.h"
#include "physics/eos/IdealGas.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"

#include <cuda_runtime.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void check(cudaError_t result)
{
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}
void require(bool passed, const char* message)
{
    if (!passed) throw std::runtime_error(message);
}
template<class T> struct Buffer {
    T* data = nullptr;
    explicit Buffer(std::size_t count)
    { check(cudaMalloc(reinterpret_cast<void**>(&data), count * sizeof(T))); }
    ~Buffer() { cudaFree(data); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

enum class Fault { None, GhostPressure, NegativeFinitePressure, FaceEnergy, FaceDerivative, GeometryPressure };
struct ProbeEos : IdealGasView {
    Fault fault = Fault::None;
    ARCH_INLINE double get_pressure(const FluidVector& value, const double* x) const
    {
        if ((fault == Fault::GhostPressure && value.rho == 2.0)
            || fault == Fault::GeometryPressure)
            return std::numeric_limits<double>::quiet_NaN();
        if (fault == Fault::NegativeFinitePressure) return -1.0;
        return IdealGasView::get_pressure(value, x);
    }
    ARCH_INLINE double get_total_energy_primitive(
        double rho, double u, double v, double w, double pressure, const double* x) const
    {
        return fault == Fault::FaceEnergy ? std::numeric_limits<double>::quiet_NaN()
            : IdealGasView::get_total_energy_primitive(rho, u, v, w, pressure, x);
    }
    ARCH_INLINE double get_dp_drho_e(double rho, double energy, const double* x) const
    {
        return fault == Fault::FaceDerivative ? std::numeric_limits<double>::quiet_NaN()
            : IdealGasView::get_dp_drho_e(rho, energy, x);
    }
};

struct LeafResult {
    FluidVector left{}, right{}, plain_left{}, plain_right{};
    FluidVector geometric{}, plain_geometric{}, flux{};
    double floored_pressure = 0.0, valid_after_failure = 0.0;
    int status_after_operation = 0, status_after_valid_query = 0;
};

__global__ void leaf_kernel(Fault fault, int* status, LeafResult* result)
{
    // A six-cell PPM stencil, with the far-left (ghost) cell distinguishable
    // from the two reconstructed interior states. No out-of-bounds fixture.
    constexpr int cells = 6;
    double storage[6 * cells]{};
    arch::cuda::DeviceStateView state{storage, storage + cells, storage + 2 * cells,
        storage + 3 * cells, storage + 4 * cells, storage + 5 * cells,
        nullptr, cells, 0};
    for (int cell = 0; cell < cells; ++cell)
        state.store(cell, {cell == 0 ? 2.0 : 1.0, 0.0, 0.0, 0.0, 2.5});
    ProbeEos plain;
    plain.fault = fault;
    const auto checked = arch::cuda::make_checked_hydro_eos(plain, status);
    double unused[1]{};
    if (fault == Fault::GeometryPressure) {
        GridMetrics::GeometryView grid{};
        grid.geometry = GridMetrics::Geometry::Spherical;
        grid.dim = 1; grid.dx1 = 1.0; grid.x1_min = 1.5;
        TimeIntegration::add_geometric_source_cell(
            state.load(2), nullptr, checked, grid, 0, 0, 0.1, result->geometric);
        result->status_after_operation = *status;
    } else {
        arch::cuda::CudaPpmReconstruction::reconstruct(state, 2, 1, checked,
            result->left, result->right, unused, unused, unused);
        if (fault == Fault::FaceDerivative)
            arch::cuda::CudaHllFlux::compute(result->left, result->right, nullptr,
                nullptr, 0, checked, 0, 0.0, result->flux, unused);
        // Observe the actual reconstruction/flux path BEFORE any extra probe;
        // the following floor check cannot manufacture its earlier latch.
        result->status_after_operation = *status;
        // This floor is deliberately the same operand order used by PPM.
        // A NaN EOS query can become finite here; the status must not be erased.
        result->floored_pressure = std::max(1.0e-13,
            checked.get_pressure(state.load(0), nullptr));
    }

    ProbeEos valid;
    const auto valid_checked = arch::cuda::make_checked_hydro_eos(valid, status);
    result->valid_after_failure = valid_checked.get_pressure(state.load(2), nullptr);
    result->status_after_valid_query = *status;

    if (fault == Fault::None || fault == Fault::NegativeFinitePressure) {
        // On the same GPU compare wrapped/unwrapped arithmetic exactly. A
        // finite negative pressure keeps the pre-existing PPM floor behavior;
        // the status wrapper is not a new physical admissibility policy.
        arch::cuda::CudaPpmReconstruction::reconstruct(state, 2, 1, plain,
            result->plain_left, result->plain_right, unused, unused, unused);
        GridMetrics::GeometryView grid{};
        grid.geometry = GridMetrics::Geometry::Cylindrical;
        grid.dim = 1; grid.dx1 = 1.0; grid.x1_min = 1.5;
        TimeIntegration::add_geometric_source_cell(
            state.load(2), nullptr, checked, grid, 0, 0, 0.1, result->geometric);
        TimeIntegration::add_geometric_source_cell(
            state.load(2), nullptr, plain, grid, 0, 0, 0.1, result->plain_geometric);
    }
}

bool equal_bits(const FluidVector& left, const FluidVector& right)
{
    const double a[]{left.rho, left.mom_u, left.mom_v, left.mom_w, left.eng};
    const double b[]{right.rho, right.mom_u, right.mom_v, right.mom_w, right.eng};
    for (int field = 0; field < 5; ++field)
        if (std::bit_cast<std::uint64_t>(a[field]) != std::bit_cast<std::uint64_t>(b[field]))
            return false;
    return true;
}

void test_hydro_leaves()
{
    Buffer<int> status(1);
    Buffer<LeafResult> output(1);
    for (const auto fault : {Fault::None, Fault::GhostPressure, Fault::NegativeFinitePressure,
                            Fault::FaceEnergy, Fault::FaceDerivative, Fault::GeometryPressure}) {
        check(cudaMemset(status.data, 0, sizeof(int)));
        check(cudaMemset(output.data, 0, sizeof(LeafResult)));
        leaf_kernel<<<1, 1>>>(fault, status.data, output.data);
        check(cudaGetLastError());
        LeafResult result;
        int final_status = -1;
        check(cudaMemcpy(&result, output.data, sizeof(result), cudaMemcpyDeviceToHost));
        check(cudaMemcpy(&final_status, status.data, sizeof(final_status), cudaMemcpyDeviceToHost));
        const bool valid = fault == Fault::None || fault == Fault::NegativeFinitePressure;
        require(result.status_after_operation == (valid ? 0 : 1)
            && result.status_after_valid_query == (valid ? 0 : 1)
            && final_status == (valid ? 0 : 1), "Hydro EOS failure was missed or cleared by a valid query");
        require(std::isfinite(result.valid_after_failure), "Valid control EOS returned nonfinite pressure");
        if (valid) {
            require(equal_bits(result.left, result.plain_left)
                && equal_bits(result.right, result.plain_right)
                && equal_bits(result.geometric, result.plain_geometric),
                "Checked EOS changed valid reconstruction, floor or geometric-source arithmetic");
        }
        if (fault == Fault::GhostPressure)
            require(result.floored_pressure == 1.0e-13
                && std::isfinite(result.left.eng) && std::isfinite(result.right.eng),
                "Ghost-NaN fixture did not exercise PPM/floor recovery to finite values");
        if (fault == Fault::NegativeFinitePressure)
            require(result.floored_pressure == 1.0e-13 && result.left.eng > 0.0,
                    "Existing finite pressure floor was changed");
        if (fault == Fault::FaceEnergy)
            require(std::isnan(result.left.eng) && std::isnan(result.right.eng),
                    "Reconstructed-face EOS failure fixture did not fire");
        if (fault == Fault::GeometryPressure)
            require(std::isnan(result.geometric.mom_u), "Geometric-stage EOS failure fixture did not fire");
    }
}

void test_host_exception_contract()
{
    for (const auto error : {tabular_eos::FreeEnergyStatus::invalid_pressure_or_energy,
                            tabular_eos::FreeEnergyStatus::invalid_heat_capacity,
                            tabular_eos::FreeEnergyStatus::invalid_derivatives,
                            tabular_eos::FreeEnergyStatus::invalid_sound_speed}) {
        for (const bool bind_status : {false, true}) {
            int marker = 17;
            bool threw = false;
            try {
                static_cast<void>(tabular_eos::checked_thermodynamics(
                    tabular_eos::free_energy_failure(error), bind_status ? &marker : nullptr));
            } catch (const std::runtime_error&) { threw = true; }
            require(threw && marker == 17,
                    "Optional device EOS latch changed the Host exception contract");
        }
    }
}

void test_cfl_reducer_latch()
{
    const double finite_candidates[]{3.0, 1.0, 2.0};
    Buffer<double> candidates(3), output(1);
    Buffer<int> status(1);
    check(cudaMemcpy(candidates.data, finite_candidates, sizeof(finite_candidates), cudaMemcpyHostToDevice));
    for (const int incoming : {1, 0}) {
        check(cudaMemcpy(status.data, &incoming, sizeof(incoming), cudaMemcpyHostToDevice));
        arch::cuda::detail::hydro_cfl_reduce_kernel<<<1, 1>>>(
            candidates.data, 3, 0.5, output.data, status.data);
        check(cudaGetLastError());
        int actual_status = -1;
        double actual = 0.0;
        check(cudaMemcpy(&actual_status, status.data, sizeof(actual_status), cudaMemcpyDeviceToHost));
        check(cudaMemcpy(&actual, output.data, sizeof(actual), cudaMemcpyDeviceToHost));
        if (incoming != 0)
            require(actual_status == static_cast<int>(arch::reduction::ReductionStatus::NanRejected)
                && std::isnan(actual), "Finite CFL candidates erased an earlier EOS failure");
        else
            require(actual_status == static_cast<int>(arch::reduction::ReductionStatus::Ok)
                && actual == 0.5, "Cleared CFL reducer changed its valid minimum");
    }
}

struct TableResult {
    double invalid = 0.0, valid = 0.0, plain_valid = 0.0;
    int after_invalid = 0, after_valid = 0;
};
template<class View>
ARCH_INLINE double direct_free_pressure(const View& view)
{
    if constexpr (requires { view.free_energy_state(1.0, 1.0, 0.5); })
        return view.free_energy_state(1.0, 1.0, 0.5).pressure;
    else
        return view.free_energy_state(1.0, 1.0, 14.0, 7.0).pressure;
}

template<class View>
struct FloorRecoveringTableEos : View {
    ARCH_INLINE double get_pressure_from_rho_T(double, double, const double*) const
    {
        // Test-only recovery around the REAL Tabular failure boundary. If the
        // checked wrapper forgets to bind the inherited leaf status pointer,
        // this finite return would conceal the failure from scalar inspection.
        return std::max(1.0e-13, direct_free_pressure(*this));
    }
};

template<class View>
__global__ void tabular_kernel(View valid, int* status, int mode, bool fail,
                               TableResult* result)
{
    result->plain_valid = direct_free_pressure(valid);
    View first = valid;
    if (fail) first.free_energy_fields[tabular_eos::Fyy] = first.free_energy_fields[tabular_eos::Fy];
    if (mode == 2) {
        FloorRecoveringTableEos<View> recovering;
        static_cast<View&>(recovering) = first;
        const auto checked = arch::cuda::make_checked_hydro_eos(recovering, status);
        result->invalid = checked.get_pressure_from_rho_T(1.0, 1.0, nullptr);
    } else if (mode == 1) {
        // Only the wrapper binds the leaf status hook in this branch.
        const auto checked = arch::cuda::make_checked_hydro_eos(first, status);
        result->invalid = checked.get_pressure_from_rho_T(1.0, 1.0, nullptr);
    } else {
        first.device_error_status = status;
        result->invalid = direct_free_pressure(first);
    }
    result->after_invalid = *status;
    const auto checked_valid = arch::cuda::make_checked_hydro_eos(valid, status);
    result->valid = checked_valid.get_pressure_from_rho_T(1.0, 1.0, nullptr);
    result->after_valid = *status;
}

template<class View>
void test_table(View view, int extent)
{
    // At rho=T=1 this derivative jet gives P=2,E=3,Cv=3,cs^2=10/3.
    // Constant nodal fixtures suffice because the query is exactly on the
    // lower rho/T knot. They are not a second interpolator/EOS implementation.
    const double fields[tabular_eos::FieldCount]{3.0, 2.0, 0.0, 0.0, 2.0, -3.0, 0.0, 0.0, 0.0};
    std::vector<double> host(tabular_eos::FieldCount * extent);
    for (int field = 0; field < tabular_eos::FieldCount; ++field)
        std::fill_n(host.data() + field * extent, extent, fields[field]);
    Buffer<double> storage(host.size());
    check(cudaMemcpy(storage.data, host.data(), host.size() * sizeof(double), cudaMemcpyHostToDevice));
    view.uses_free_energy = true;
    for (int field = 0; field < tabular_eos::FieldCount; ++field)
        view.free_energy_fields[field] = storage.data + field * extent;
    Buffer<int> status(1);
    Buffer<TableResult> output(1);
    for (int mode : {0, 1, 2}) {
        for (bool fail : {false, true}) {
            check(cudaMemset(status.data, 0, sizeof(int)));
            tabular_kernel<<<1, 1>>>(view, status.data, mode, fail, output.data);
            check(cudaGetLastError());
            TableResult result;
            check(cudaMemcpy(&result, output.data, sizeof(result), cudaMemcpyDeviceToHost));
            require(result.after_invalid == (fail ? 1 : 0)
                && result.after_valid == (fail ? 1 : 0), "Tabular free-energy leaf failure was not sticky");
            require(std::isfinite(result.valid)
                && std::bit_cast<std::uint64_t>(result.valid)
                    == std::bit_cast<std::uint64_t>(result.plain_valid),
                    "Valid Tabular pressure changed after failure instrumentation");
            require(std::abs(result.valid - 2.0)
                    <= 32.0 * std::numeric_limits<double>::epsilon(),
                    "Tabular fixture does not recover its analytic nodal pressure");
            const bool expected_value = fail
                ? (mode == 2 ? result.invalid == 1.0e-13 : std::isnan(result.invalid))
                : result.invalid == result.valid;
            require(expected_value,
                    "Tabular free-energy failure boundary or valid control changed");
        }
    }
}

void test_tabular_leaves()
{
    Tabular3DEOSView view3{};
    view3.n_rho = view3.n_T = view3.n_X = 2;
    view3.log_rho_max = view3.log_T_max = view3.dlog_rho = view3.dlog_T = 1.0;
    view3.X_max = view3.dX = 1.0;
    view3.target_species_id = -1;
    test_table(view3, 8);
    Tabular4DEOSView view4{};
    view4.n_rho = view4.n_T = view4.n_A = view4.n_Z = 2;
    view4.log_rho_max = view4.log_T_max = view4.dlog_rho = view4.dlog_T = 1.0;
    view4.A_min = 14.0; view4.A_max = 15.0; view4.dA = 1.0;
    view4.Z_min = 7.0; view4.Z_max = 8.0; view4.dZ = 1.0;
    test_table(view4, 16);
}
} // namespace

int main()
{
    try { test_host_exception_contract(); }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    int devices = 0;
    const auto probe = cudaGetDeviceCount(&devices);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && devices == 0)) return 77;
    if (probe != cudaSuccess) {
        std::cerr << "CUDA device probe failed: " << cudaGetErrorString(probe) << '\n';
        return 1;
    }
    try {
        test_hydro_leaves();
        test_cfl_reducer_latch();
        test_tabular_leaves();
        std::cout << "Hydro checked-EOS ghost/PPM/face/geometry/Tabular sticky failure controls passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
