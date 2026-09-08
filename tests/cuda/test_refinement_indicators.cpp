/**
 * @file test_refinement_indicators.cpp
 * @brief Compare production GPU refinement indicators with shared host math.
 *
 * Cases cover different fields and species layouts, including recovered
 * tabular temperatures and propagation of invalid EOS states.
 */
#include "cuda/amr/RefinementIndicators.h"
#include "cuda/hydro/GridGeometryAdapter.cuh"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(cudaError_t status) {
    if (status != cudaSuccess) throw std::runtime_error(cudaGetErrorString(status));
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class T> struct Buffer {
    T* data = nullptr;
    explicit Buffer(std::size_t count) {
        check(cudaMalloc(reinterpret_cast<void**>(&data), count * sizeof(T)));
    }
    ~Buffer() { if (data) cudaFree(data); }
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
};

void run(int dimension, const std::string& geometry) {
    Grid grid(2, 1.0, 2.0, 0.4, 1.1, 0.2, 0.8);
    grid.dim = dimension;
    grid.geometry = geometry;
    grid.InitializeTopology();
    const int cells = grid.GetTotalSize();
    constexpr int species_count = 2;
    constexpr int field_count = 6 + species_count;
    std::vector<double> fields(static_cast<std::size_t>(field_count) * cells);
    for (int k = 0; k < grid.GetTotalZ(); ++k)
        for (int j = 0; j < grid.GetTotalY(); ++j)
            for (int i = 0; i < grid.GetTotalX(); ++i) {
                const int cell = grid.GetIndex(i, j, k);
                const double x = grid.GetCellCenterX(i);
                const double y = grid.GetCellCenterY(j);
                const double z = grid.GetCellCenterZ(k);
                const double density = 2.0 + x * x + y * y + z * z;
                fields[cell] = density;
                fields[cells + cell] = density * (0.2 + x * y);
                fields[2 * cells + cell] = density * (0.3 + x * x);
                fields[3 * cells + cell] = density * (0.4 + z * x);
                fields[4 * cells + cell] = 100.0 + x * x * x + y * y;
                fields[5 * cells + cell] = std::sin(x * x);
                fields[6 * cells + cell] = 0.2 + 0.05 * std::sin(x + y);
                fields[7 * cells + cell] = 1.0 - fields[6 * cells + cell];
            }
    IdealGasView eos;
    std::vector<double> thermo(3 * cells);
    for (int cell = 0; cell < cells; ++cell) {
        const FluidVector fluid{fields[cell], fields[cells + cell],
            fields[2 * cells + cell], fields[3 * cells + cell], fields[4 * cells + cell]};
        const auto value = amr::indicator::thermodynamics(fluid, nullptr, eos, true, true, true);
        thermo[cell] = value.pressure;
        thermo[cells + cell] = value.temperature;
        thermo[2 * cells + cell] = value.gamma1;
    }
    AmrConfig config;
    config.refine_on_p = config.refine_on_temp = config.refine_on_eng = true;
    config.refine_on_velx = config.refine_on_vorticity = config.refine_on_div_v = true;
    config.refine_on_vely = dimension >= 2;
    config.refine_on_velz = dimension == 3;
    config.refine_on_entropy = config.refine_on_enuc = config.refine_on_species = true;
    const int species[] = {0, 1};
    const auto selections = amr::indicator::make_selection(config, dimension, species);
    constexpr double density_floor = 1.e-12;
    const amr::indicator::StateView host{
        fields.data(), {fields.data() + cells, fields.data() + 2 * cells,
            fields.data() + 3 * cells}, fields.data() + 4 * cells,
        fields.data() + 5 * cells, fields.data() + 6 * cells,
        thermo.data(), thermo.data() + cells, thermo.data() + 2 * cells, cells,
        grid.Is(), grid.Ie(), grid.Js(), grid.Je(), grid.Ks(), grid.Ke(), density_floor,
        species_count};
    Buffer<double> device_fields(fields.size()), device_thermo(thermo.size()),
        composition(static_cast<std::size_t>(cells) * species_count), errors(cells), summary(1);
    Buffer<amr::indicator::Selection> selection(1);
    Buffer<int> eos_status(1);
    check(cudaMemcpy(device_fields.data, fields.data(), fields.size() * sizeof(double), cudaMemcpyHostToDevice));
    arch::cuda::DeviceStateView device{device_fields.data, device_fields.data + cells,
        device_fields.data + 2 * cells, device_fields.data + 3 * cells,
        device_fields.data + 4 * cells, device_fields.data + 5 * cells,
        device_fields.data + 6 * cells, cells, species_count};
    arch::cuda::DeviceIndicatorWorkspace workspace{selection.data, 1, density_floor,
        true, true, true, device_thermo.data, composition.data, errors.data, summary.data,
        eos_status.data};
    for (const auto selected : selections) {
        double expected = 0.0;
        for (int k = grid.Ks(); k < grid.Ke(); ++k)
            for (int j = grid.Js(); j < grid.Je(); ++j)
                for (int i = grid.Is(); i < grid.Ie(); ++i) {
                    const auto value = amr::indicator::cell_error(host, grid, &selected, 1, i, j, k);
                    require(std::isfinite(value), "nonfinite Host fixture");
                    expected = std::max(expected, value);
                }
        check(cudaMemcpy(selection.data, &selected, sizeof(selected), cudaMemcpyHostToDevice));
        check(arch::cuda::launch_cuda_refinement_indicators(device,
            arch::cuda::make_device_grid_view(grid), eos, workspace, nullptr));
        check(cudaDeviceSynchronize());
        double actual = 0.0;
        check(cudaMemcpy(&actual, summary.data, sizeof(actual), cudaMemcpyDeviceToHost));
        require(std::isfinite(actual) && std::abs(actual - expected) <= 2.e-13,
            "CUDA AMR indicator differs from Host math");
    }
    // A nonfinite selected field must propagate to the backend, not disappear
    // behind a max reduction (the Host executor rejects the same sentinel).
    fields[grid.GetIndex(grid.Is(), grid.Js(), grid.Ks())] = std::numeric_limits<double>::quiet_NaN();
    check(cudaMemcpy(device_fields.data, fields.data(), fields.size() * sizeof(double), cudaMemcpyHostToDevice));
    const amr::indicator::Selection density{amr::indicator::Field::Density};
    check(cudaMemcpy(selection.data, &density, sizeof(density), cudaMemcpyHostToDevice));
    workspace.pressure = workspace.temperature = workspace.gamma1 = false;
    check(arch::cuda::launch_cuda_refinement_indicators(device,
        arch::cuda::make_device_grid_view(grid), eos, workspace, nullptr));
    check(cudaDeviceSynchronize());
    double invalid = 0.0;
    check(cudaMemcpy(&invalid, summary.data, sizeof(invalid), cudaMemcpyDeviceToHost));
    require(!std::isfinite(invalid), "nonfinite indicator was silently accepted");
}

template<class Eos>
void test_recovered_tabular_temperature(Eos eos, int compositions) {
    Grid grid(1, 1.0, 2.0);
    grid.dim = 1;
    grid.InitializeTopology();
    const int cells = grid.GetTotalSize();
    std::vector<double> fields(6 * cells, 0.0);
    std::fill_n(fields.data(), cells, 1.0);
    std::fill_n(fields.data() + 4 * cells, cells, 3.0);
    Buffer<double> device_fields(fields.size()), thermo(3 * cells), errors(cells), summary(1);
    Buffer<int> eos_status(1);
    Buffer<amr::indicator::Selection> selection(1);
    const amr::indicator::Selection temperature{amr::indicator::Field::Temperature};
    check(cudaMemcpy(selection.data, &temperature, sizeof(temperature), cudaMemcpyHostToDevice));
    check(cudaMemcpy(device_fields.data, fields.data(), fields.size() * sizeof(double), cudaMemcpyHostToDevice));
    arch::cuda::DeviceStateView state{device_fields.data, device_fields.data + cells,
        device_fields.data + 2 * cells, device_fields.data + 3 * cells,
        device_fields.data + 4 * cells, device_fields.data + 5 * cells,
        nullptr, cells, 0};
    arch::cuda::DeviceIndicatorWorkspace workspace{selection.data, 1, 1.e-12,
        false, true, false, thermo.data, nullptr, errors.data, summary.data, eos_status.data};

    // At both rho knots P=2 and e=3. The valid upper-temperature knot has Cv>0;
    // only the lower-temperature knot is made invalid. Device inversion first
    // encounters that failed boundary query, then converges to finite T=10.
    // A final-only temperature/Lohner check would therefore report zero error.
    const int extent = 4 * compositions;
    const double jets[]{3.0, 2.0, 0.0, 0.0, 2.0, -3.0, 0.0, 0.0, 0.0};
    std::vector<double> table(tabular_eos::FieldCount * extent);
    Buffer<double> device_table(table.size());
    eos.uses_free_energy = true;
    for (const bool fail : {true, false}) {
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            std::fill_n(table.data() + field * extent, extent, jets[field]);
        if (fail)
            for (int rho = 0; rho < 2; ++rho)
                std::fill_n(table.data() + tabular_eos::Fyy * extent
                    + rho * 2 * compositions, compositions, 0.0);
        Eos host = eos;
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            host.free_energy_fields[field] = table.data() + field * extent;
        bool host_threw = false;
        try { static_cast<void>(host.get_temperature(1.0, 3.0, nullptr)); }
        catch (const std::runtime_error&) { host_threw = true; }
        require(host_threw == fail, "Tabular temperature fixture does not exercise Host failure");
        check(cudaMemcpy(device_table.data, table.data(), table.size() * sizeof(double), cudaMemcpyHostToDevice));
        for (int field = 0; field < tabular_eos::FieldCount; ++field)
            eos.free_energy_fields[field] = device_table.data + field * extent;
        check(arch::cuda::launch_cuda_refinement_indicators(state,
            arch::cuda::make_device_grid_view(grid), eos, workspace, nullptr));
        std::vector<double> actual_thermo(3 * cells);
        double actual = 0.0;
        int status = -1;
        check(cudaMemcpy(actual_thermo.data(), thermo.data, actual_thermo.size() * sizeof(double), cudaMemcpyDeviceToHost));
        check(cudaMemcpy(&actual, summary.data, sizeof(actual), cudaMemcpyDeviceToHost));
        check(cudaMemcpy(&status, eos_status.data, sizeof(status), cudaMemcpyDeviceToHost));
        for (int cell = 0; cell < cells; ++cell)
            require(std::isfinite(actual_thermo[cells + cell])
                && std::abs(actual_thermo[cells + cell] - (fail ? 10.0 : 1.0)) < 1.e-10,
                "Tabular fixture did not recover a finite temperature");
        require(status == (fail ? 1 : 0) && (fail ? std::isnan(actual) : actual == 0.0),
            "AMR discarded an intermediate EOS failure or failed to reset the next launch");
    }
    workspace.eos_status = nullptr;
    require(arch::cuda::launch_cuda_refinement_indicators(state,
        arch::cuda::make_device_grid_view(grid), eos, workspace, nullptr) == cudaErrorInvalidValue,
        "Thermodynamic indicators accepted an unbound failure latch");
}

void test_tabular_temperature_failures() {
    Tabular3DEOSView eos3{};
    eos3.n_rho = eos3.n_T = eos3.n_X = 2;
    eos3.log_rho_max = eos3.log_T_max = eos3.dlog_rho = eos3.dlog_T = 1.0;
    eos3.X_max = eos3.dX = 1.0;
    eos3.target_species_id = -1;
    test_recovered_tabular_temperature(eos3, 2);
    Tabular4DEOSView eos4{};
    eos4.n_rho = eos4.n_T = eos4.n_A = eos4.n_Z = 2;
    eos4.log_rho_max = eos4.log_T_max = eos4.dlog_rho = eos4.dlog_T = 1.0;
    eos4.A_min = 14.0; eos4.A_max = 15.0; eos4.dA = 1.0;
    eos4.Z_min = 7.0; eos4.Z_max = 8.0; eos4.dZ = 1.0;
    test_recovered_tabular_temperature(eos4, 4);
}
}

int main() {
    int devices = 0;
    const auto probe = cudaGetDeviceCount(&devices);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && devices == 0)) return 77;
    if (probe != cudaSuccess) {
        std::cerr << cudaGetErrorString(probe) << '\n';
        return 1;
    }
    try {
        require(std::abs(amr::indicator::loehner_error(1.0, 2.0, 4.0) - 1.0 / 3.09) < 1.e-15,
            "frozen Lohner stencil changed");
        require(amr::indicator::refinement_flag(0.8, 1, 0, 2, 0.8, 0.2) == 0,
            "refinement equality boundary changed");
        for (int dimension = 1; dimension <= 3; ++dimension)
            for (const auto* geometry : {"cartesian", "cylindrical", "spherical"})
                run(dimension, geometry);
        test_tabular_temperature_failures();
        std::cout << "CUDA_REFINEMENT_INDICATORS_PASS\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
