/** Host-compiled smoke test: the factorization and solves execute in cuDSS/GPU. */
#include "cuda/microphysics/CuDssSparseSolver.h"
#include "numerics/linalg/CsrMatrixView.h"
#include "numerics/linalg/LinearEquilibration.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void check(cudaError_t error)
{
    if (error != cudaSuccess) throw std::runtime_error(cudaGetErrorString(error));
}
void require(bool passed, const char* reason)
{
    if (!passed) throw std::runtime_error(reason);
}
struct Stream
{
    cudaStream_t value{};
    Stream() { check(cudaStreamCreate(&value)); }
    ~Stream() { cudaStreamDestroy(value); }
};
template <class T> struct Buffer
{
    T* value = nullptr;
    explicit Buffer(std::size_t count) { check(cudaMalloc(reinterpret_cast<void**>(&value), count * sizeof(T))); }
    ~Buffer() { cudaFree(value); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};
void require_result(const arch::cuda::CuDssResult& result)
{
    result.require_success();
}

void equilibration_math_contract()
{
    using arch::linalg::equilibration_divisor;
    using arch::linalg::equilibrated_value;
    const double row[]{-2.0, 0.0, 8.0, 4.0};
    const int permutation[]{3, 0, 1, 2};
    require(equilibration_divisor(row, 0, 4) == 8.0
        && equilibration_divisor(row, 0, 2, permutation) == 4.0,
        "Contiguous and permuted equation-unit scales disagree with exact maxima");
    double scaled = 0.0;
    require(equilibrated_value(-2.0, 8.0, scaled) && scaled == -0.25,
            "Binary-exact normalization changed sign or units");
    const double zero[]{0.0, -0.0};
    require(equilibration_divisor(zero, 0, 2) == 1.0,
            "A zero row was regularized instead of retaining a singular equation");
    const double extreme[]{std::numeric_limits<double>::denorm_min(),
                           std::numeric_limits<double>::max()};
    require(equilibration_divisor(extreme, 0, 2) == 1.0,
            "Normalization erased a representable coefficient through underflow");
    for (double value : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity()}) {
        require(!std::isfinite(equilibration_divisor(&value, 0, 1)),
                "Nonfinite matrix coefficient did not invalidate normalization");
        require(!equilibrated_value(value, 1.0, scaled), "Nonfinite vector accepted");
    }
    require(!equilibrated_value(extreme[0], 2.0, scaled)
        && !equilibrated_value(extreme[1], 0.5, scaled)
        && !equilibrated_value(1.0, 0.0, scaled),
        "Unrepresentable normalization or zero divisor was silently accepted");
}

void run(int extent)
{
    Stream stream;
    std::vector<int> rows(1, 0), columns;
    std::vector<double> values;
    for (int row = 0; row < extent; ++row) {
        if (row > 0) { columns.push_back(row - 1); values.push_back(-0.75); }
        columns.push_back(row); values.push_back(4.0);
        if (row + 1 < extent) { columns.push_back(row + 1); values.push_back(-1.25); }
        rows.push_back(static_cast<int>(columns.size()));
    }
    Buffer<int> device_rows(rows.size()), device_columns(columns.size());
    Buffer<double> device_values(values.size()), device_rhs(extent), device_solution(extent);
    check(cudaMemcpyAsync(device_rows.value, rows.data(), rows.size() * sizeof(int), cudaMemcpyHostToDevice, stream.value));
    check(cudaMemcpyAsync(device_columns.value, columns.data(), columns.size() * sizeof(int), cudaMemcpyHostToDevice, stream.value));
    check(cudaMemcpyAsync(device_values.value, values.data(), values.size() * sizeof(double), cudaMemcpyHostToDevice, stream.value));
    arch::cuda::CuDssSparseSolver solver(extent, static_cast<int>(columns.size()),
        device_rows.value, device_columns.value, device_values.value,
        device_rhs.value, device_solution.value, stream.value);
    const auto metadata_bytes = (rows.size() + columns.size()) * sizeof(int);
    require(solver.bytes_d2h() == metadata_bytes && solver.bytes_h2d() == metadata_bytes
        && solver.kernel_count() == 0 && solver.synchronization_count() == 2,
        "Provider construction omitted or duplicated its immutable metadata transfers/fences");
    bool rejected = false;
    try { solver.solve(device_rhs.value, device_solution.value, 1); }
    catch (const std::logic_error&) { rejected = true; }
    require(rejected, "Solve without numerical factors was accepted");
    for (int round = 0; round < 3; ++round) {
        std::vector<double> exact(extent), rhs(extent, 0.0), solved(extent);
        for (int row = 0; row < extent; ++row) exact[row] = 1.0 + 0.001 * row + 0.25 * round;
        if (round == 2) {
            for (int row = 0; row < extent; ++row)
                for (int slot = rows[row]; slot < rows[row + 1]; ++slot)
                    if (columns[slot] == row) values[slot] = 6.0;
            check(cudaMemcpyAsync(device_values.value, values.data(), values.size() * sizeof(double), cudaMemcpyHostToDevice, stream.value));
        }
        for (int row = 0; row < extent; ++row)
            for (int slot = rows[row]; slot < rows[row + 1]; ++slot)
                rhs[row] += values[slot] * exact[columns[slot]];
        check(cudaMemcpyAsync(device_rhs.value, rhs.data(), rhs.size() * sizeof(double), cudaMemcpyHostToDevice, stream.value));
        const std::uint64_t token = round == 2 ? 2 : 1;
        const auto kernels_before = solver.kernel_count();
        const auto downloaded_before = solver.bytes_d2h();
        if (round != 1) require_result(solver.factorize(device_values.value, token));
        require_result(solver.solve(device_rhs.value, device_solution.value, token));
        require(solver.kernel_count() - kernels_before == (round == 1 ? 2 : 4)
            && solver.bytes_d2h() - downloaded_before == sizeof(int) * (round == 0 ? 3 : round == 1 ? 1 : 2)
            && solver.bytes_h2d() == metadata_bytes,
            "Provider factor/reuse omitted preparation kernels or scalar transfers, or recopied numeric Host state");
        check(cudaMemcpyAsync(solved.data(), device_solution.value, solved.size() * sizeof(double), cudaMemcpyDeviceToHost, stream.value));
        check(cudaStreamSynchronize(stream.value));
        double error = 0.0, residual = 0.0;
        for (int row = 0; row < extent; ++row) {
            require(std::isfinite(solved[row]), "Sparse GPU solve returned nonfinite state");
            error = std::max(error, std::abs(solved[row] - exact[row]));
            double sum = 0.0;
            for (int slot = rows[row]; slot < rows[row + 1]; ++slot)
                sum += values[slot] * solved[columns[slot]];
            residual = std::max(residual, std::abs(sum - rhs[row]));
        }
        require(error < 1.0e-11 && residual < 1.0e-11,
                "Sparse GPU solve failed the existing CPU KLU manufactured-system scale");
    }
    require(solver.analysis_count() == 1, "Numeric updates repeated symbolic analysis");
    require(solver.peak_device_bytes() > 0, "cuDSS omitted the factor memory estimate");
    std::cout << "CUDSS_MEMORY rows=" << extent << " nonzeros=" << columns.size()
              << " factor_and_equilibration_estimated_peak_bytes=" << solver.peak_device_bytes() << '\n';
    solver.invalidate();
    rejected = false;
    try { solver.solve(device_rhs.value, device_solution.value, 2); }
    catch (const std::logic_error&) { rejected = true; }
    require(rejected, "Externally rejected factors remained usable");
    require_result(solver.factorize_and_solve(device_values.value, device_rhs.value,
                                             device_solution.value, 2));
    require(solver.analysis_count() == 2,
            "External residual rejection did not rebuild native analysis state");
    std::vector<double> recovered(extent);
    check(cudaMemcpyAsync(recovered.data(), device_solution.value, recovered.size() * sizeof(double),
                          cudaMemcpyDeviceToHost, stream.value));
    check(cudaStreamSynchronize(stream.value));
    for (int row = 0; row < extent; ++row)
        require(std::abs(recovered[row] - (1.5 + 0.001 * row)) < 1.0e-11,
                "Reanalysis after external rejection corrupted the recovered solution");
    rejected = false;
    try { solver.solve(device_rhs.value, device_solution.value, 1); }
    catch (const std::logic_error&) { rejected = true; }
    require(rejected, "Stale factor token was accepted");
    rejected = false;
    try { solver.factorize(values.data(), 3); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Host numeric buffer was accepted as GPU execution");
    auto invalid_values = values;
    invalid_values[0] = std::numeric_limits<double>::quiet_NaN();
    check(cudaMemcpyAsync(device_values.value, invalid_values.data(), values.size() * sizeof(double),
                          cudaMemcpyHostToDevice, stream.value));
    require(!solver.factorize(device_values.value, 3).success(),
            "Device normalization accepted a nonfinite matrix coefficient");
    check(cudaMemcpyAsync(device_values.value, values.data(), values.size() * sizeof(double),
                          cudaMemcpyHostToDevice, stream.value));
    require_result(solver.factorize(device_values.value, 4));
    std::vector<double> tiny_rhs(extent, 0.0);
    tiny_rhs[0] = std::numeric_limits<double>::denorm_min();
    check(cudaMemcpyAsync(device_rhs.value, tiny_rhs.data(), tiny_rhs.size() * sizeof(double),
                          cudaMemcpyHostToDevice, stream.value));
    require(!solver.solve(device_rhs.value, device_solution.value, 4).success(),
            "Device RHS normalization silently erased a nonzero forcing");
    rejected = false;
    try { solver.solve(device_rhs.value, device_solution.value, 4); }
    catch (const std::logic_error&) { rejected = true; }
    require(rejected, "Failed normalization left numerical factors usable");
    // Recover the same owner after either error, without inheriting its latch.
    std::vector<double> zero_rhs(extent, 0.0), zero_solution(extent), preserved(values.size());
    check(cudaMemcpyAsync(device_rhs.value, zero_rhs.data(), zero_rhs.size() * sizeof(double),
                          cudaMemcpyHostToDevice, stream.value));
    require_result(solver.factorize_and_solve(device_values.value, device_rhs.value,
                                             device_solution.value, 5));
    check(cudaMemcpyAsync(zero_solution.data(), device_solution.value, zero_solution.size() * sizeof(double),
                          cudaMemcpyDeviceToHost, stream.value));
    check(cudaMemcpyAsync(preserved.data(), device_values.value, preserved.size() * sizeof(double),
                          cudaMemcpyDeviceToHost, stream.value));
    check(cudaStreamSynchronize(stream.value));
    require(values == preserved, "Equilibration modified the caller's original coefficients");
    require(std::all_of(zero_solution.begin(), zero_solution.end(),
                        [](double value) { return value == 0.0; }),
            "Recovery failed the exact homogeneous system");
    std::cout << "cuDSS N=" << extent << ": solve/reuse/refactor and negative contracts passed\n";
}

enum class MultiscaleCase { Thermal, IntegratedSource, TraceChain, DisconnectedUnits };

void run_multiscale_system(MultiscaleCase model)
{
    // A manufactured arrowhead system with a known solution, not a nuclear
    // Jacobian snapshot or a second ODE. The final unknown has a temperature-
    // sized scale while preceding unknowns have composition-correction scales.
    // Large final-row and tiny final-column entries expose loss of componentwise
    // accuracy that an absolute/normwise residual check can miss.
    constexpr int extent = 32;
    const bool include_integrated_source = model == MultiscaleCase::IntegratedSource;
    const bool rescaled = model == MultiscaleCase::DisconnectedUnits;
    const bool trace_chain = model == MultiscaleCase::TraceChain || rescaled;
    const int thermal = extent - 1 - static_cast<int>(include_integrated_source);
    Stream stream;
    std::vector<int> rows{0}, columns;
    std::vector<double> values;
    for (int row = 0; row < extent; ++row) {
        if (row < thermal) {
            // Binary-exact conservative chain: all downstream RHS entries are
            // exactly zero, yet its known corrections reach about 1e-43. The
            // thermal row couples this hierarchy into one nonsymmetric system.
            if (trace_chain && row > 0) {
                columns.push_back(row - 1);
                values.push_back(rescaled && row == thermal / 2 ? 0.0 : -std::ldexp(1.0, -4));
            }
            columns.push_back(row); values.push_back(trace_chain ? 1.0 : 1.0 + (row + 1) * 1.0e-5);
            if (!trace_chain && row + 1 < thermal) {
                columns.push_back(row + 1); values.push_back(-2.0e-6);
            }
            if (!trace_chain || row == 0) {
                columns.push_back(thermal);
                values.push_back(trace_chain ? std::ldexp(1.0, -48) : (row + 1) * 1.0e-15);
            }
        } else if (row == thermal) {
            for (int column = 0; column < thermal; ++column) {
                columns.push_back(column); values.push_back(-(column + 1) * 1.1e7);
            }
            columns.push_back(thermal); values.push_back(1.2);
        } else {
            // A third physical unit, integrated energy, makes a triangular
            // extension of the same manufactured system. Its known solution
            // tests provider conditioning without importing reaction snapshots.
            for (int column = 0; column < thermal; ++column) {
                columns.push_back(column); values.push_back((column + 1) * 1.3e16);
            }
            columns.push_back(thermal); values.push_back(9.0e4);
            columns.push_back(row); values.push_back(1.0);
        }
        rows.push_back(static_cast<int>(columns.size()));
    }
    if (rescaled)
        for (int row = 0; row < extent; ++row)
            for (int slot = rows[row]; slot < rows[row + 1]; ++slot)
                values[slot] = std::ldexp(values[slot], 80 * (row % 3 - 1));
    CsrMatrixView<extent> original{rows.data(), columns.data(), values.data(),
        static_cast<int>(values.size()), true};
    Buffer<int> device_rows(rows.size()), device_columns(columns.size());
    Buffer<double> device_values(values.size()), device_rhs(extent), device_solution(extent);
    check(cudaMemcpy(device_rows.value, rows.data(), rows.size() * sizeof(int), cudaMemcpyHostToDevice));
    check(cudaMemcpy(device_columns.value, columns.data(), columns.size() * sizeof(int), cudaMemcpyHostToDevice));
    check(cudaMemcpy(device_values.value, values.data(), values.size() * sizeof(double), cudaMemcpyHostToDevice));
    arch::cuda::CuDssSparseSolver solver(extent, static_cast<int>(values.size()),
        device_rows.value, device_columns.value, device_values.value,
        device_rhs.value, device_solution.value, stream.value);
    require_result(solver.factorize(device_values.value, 1));
    for (int round = 0; round < 2; ++round) {
        std::vector<double> exact(extent), rhs(extent, 0.0), solved(extent);
        for (int row = 0; row < thermal; ++row)
            exact[row] = trace_chain ? std::ldexp(1.0 + 0.25 * round, -20 - 4 * row)
                                    : (row + 1) * 1.0e-8 * (1.0 + 0.1 * round);
        if (rescaled)
            std::fill(exact.begin() + thermal / 2, exact.begin() + thermal, 0.0);
        exact[thermal] = 2.0e4 * (1.0 + 0.25 * round);
        if (include_integrated_source) exact[extent - 1] = -1.0e13 * (1.0 + 0.2 * round);
        for (int row = 0; row < extent; ++row)
            for (int slot = rows[row]; slot < rows[row + 1]; ++slot)
                rhs[row] += values[slot] * exact[columns[slot]];
        if (trace_chain)
            for (int row = 1; row < thermal; ++row)
                require(rhs[row] == 0.0, "Trace-chain RHS is not exactly homogeneous downstream");
        require(original.solution_accurate(rhs.data(), exact.data()),
                "Manufactured multiscale exact solution does not satisfy its original matrix");
        check(cudaMemcpy(device_rhs.value, rhs.data(), rhs.size() * sizeof(double), cudaMemcpyHostToDevice));
        require_result(solver.solve(device_rhs.value, device_solution.value, 1));
        check(cudaMemcpy(solved.data(), device_solution.value, solved.size() * sizeof(double), cudaMemcpyDeviceToHost));
        if (!original.solution_accurate(rhs.data(), solved.data())) {
            for (int row = 0; row < extent; ++row) {
                long double residual = -rhs[row], scale = std::abs(rhs[row]);
                for (int slot = rows[row]; slot < rows[row + 1]; ++slot) {
                    const long double term = static_cast<long double>(values[slot])
                        * solved[columns[slot]];
                    residual += term;
                    scale += std::abs(term);
                }
                if (std::abs(residual) > 64.0L * (extent + 1)
                    * std::numeric_limits<double>::epsilon() * scale)
                    std::cerr << "multiscale residual row=" << row
                        << " backward_error=" << std::abs(residual) / scale
                        << " expected=" << exact[row] << " actual=" << solved[row] << '\n';
            }
        }
        require(original.solution_accurate(rhs.data(), solved.data()),
                "Multiscale cuDSS solution fails the unchanged common componentwise residual gate");
        std::vector<double> preserved_rhs(extent), preserved_values(values.size());
        check(cudaMemcpy(preserved_rhs.data(), device_rhs.value, rhs.size() * sizeof(double),
                         cudaMemcpyDeviceToHost));
        check(cudaMemcpy(preserved_values.data(), device_values.value, values.size() * sizeof(double),
                         cudaMemcpyDeviceToHost));
        require(preserved_rhs == rhs && preserved_values == values,
                "Equilibration modified original residual-check inputs");
        // Relative, per-component forward error also protects the tiny unknowns.
        // No max(1, |x|) floor may hide their loss underneath the thermal scale.
        constexpr double relative_roundoff = 64.0 * (extent + 1)
            * std::numeric_limits<double>::epsilon();
        for (int row = 0; row < extent; ++row)
            require(std::isfinite(solved[row])
                && std::abs(solved[row] - exact[row]) <= relative_roundoff * std::abs(exact[row]),
                "Multiscale cuDSS solution lost a component's analytic correction");
        solved[0] += 1.0e-4 * exact[0];
        require(!original.solution_accurate(rhs.data(), solved.data()),
                "Componentwise residual gate accepted a perturbed tiny correction");
    }
    require(solver.analysis_count() == 1,
            "Multiscale repeated RHS unexpectedly discarded reusable symbolic structure");
    std::cout << "cuDSS multiscale N=" << extent << " integrated source=" << include_integrated_source
              << " trace chain=" << trace_chain
              << " disconnected/rescaled=" << rescaled
              << ": analytic solution, componentwise residual, factor reuse and perturbation rejection passed\n";
}
} // namespace

int main(int argc, char** argv)
{
    int devices = 0;
    const auto probe = cudaGetDeviceCount(&devices);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && devices == 0)) {
        std::cerr << "No CUDA device available for cuDSS numerical smoke\n";
        return 77;
    }
    if (probe != cudaSuccess) {
        std::cerr << "CUDA device probe failed: " << cudaGetErrorString(probe) << '\n';
        return 1;
    }
    try {
        if (argc != 1) {
            require(argc == 3 && std::string(argv[1]) == "--capacity-rows",
                    "usage: arch_cudss_sparse_solver [--capacity-rows N]");
            std::size_t consumed = 0;
            const int extent = std::stoi(argv[2], &consumed);
            require(consumed == std::string(argv[2]).size() && extent >= 32
                        && extent <= std::numeric_limits<int>::max() / 3,
                    "capacity rows must fit the provider's sparse index range and exceed the compact route");
            run(extent);
            return 0;
        }
        equilibration_math_contract();
        arch::cuda::CuDssResult{}.require_success();
        for (const auto failure : {arch::cuda::CuDssResult{1, 0, cudaSuccess},
                                  arch::cuda::CuDssResult{0, 1, cudaSuccess},
                                  arch::cuda::CuDssResult{0, -1, cudaSuccess},
                                  arch::cuda::CuDssResult{0, 0, cudaErrorMemoryAllocation}}) {
            bool rejected = false;
            try { failure.require_success(); }
            catch (const std::runtime_error&) { rejected = true; }
            require(rejected, "Opaque provider/device failure was accepted as an ODE response");
        }
        for (int extent : {32, 151, 201}) run(extent);
        run_multiscale_system(MultiscaleCase::Thermal);
        run_multiscale_system(MultiscaleCase::IntegratedSource);
        run_multiscale_system(MultiscaleCase::TraceChain);
        run_multiscale_system(MultiscaleCase::DisconnectedUnits);
        std::cout << "cuDSS compiled API version " << arch::cuda::CuDssSparseSolver::compiled_version() << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
