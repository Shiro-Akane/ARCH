// Focused production-kernel smoke, not full scientific qualification.
#include "cuda/diffusion/DiffusionKernels.cuh"
#include "cuda/hydro/HydroSourceKernels.cuh"
#include "cuda/hydro/HydroFaceKernel.cuh"
#include "physics/eos/IdealGas.h"
#include "../math/CurvilinearMetricCases.h"
#include "../math/ViscousGeometryCases.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(cudaError_t result)
{
    if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
}

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename T> class DeviceBuffer {
public:
    explicit DeviceBuffer(std::size_t count) : count_(count)
    {
        check(cudaMalloc(reinterpret_cast<void**>(&pointer_), count * sizeof(T)));
        check(cudaMemset(pointer_, 0, count * sizeof(T)));
    }
    ~DeviceBuffer() { static_cast<void>(cudaFree(pointer_)); }
    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;
    T* get() const { return pointer_; }
    void upload(const std::vector<T>& values)
    {
        require(values.size() == count_, "smoke upload shape mismatch");
        check(cudaMemcpy(pointer_, values.data(), count_ * sizeof(T), cudaMemcpyHostToDevice));
    }
    std::vector<T> download() const
    {
        std::vector<T> result(count_);
        check(cudaMemcpy(result.data(), pointer_, count_ * sizeof(T), cudaMemcpyDeviceToHost));
        return result;
    }
private:
    std::size_t count_;
    T* pointer_ = nullptr;
};

struct DeviceState {
    DeviceBuffer<double> storage;
    arch::cuda::DeviceStateView view;
    explicit DeviceState(int cells, int species = 2) : storage((6 + species) * cells), view{
        storage.get(), storage.get() + cells, storage.get() + 2 * cells,
        storage.get() + 3 * cells, storage.get() + 4 * cells,
        storage.get() + 5 * cells, storage.get() + 6 * cells, cells, species} {}
};

void initialize(FluidState& state, int cells, int species = 2)
{
    state.Preallocate(cells);
    state.InitSpecies(species);
}

std::vector<double> pack(const FluidState& state)
{
    std::vector<double> result;
    for (const auto* field : {&state.rho, &state.mom_u, &state.mom_v,
                              &state.mom_w, &state.eng, &state.enuc_rate,
                              &state.mass_fractions})
        result.insert(result.end(), field->begin(), field->end());
    return result;
}

void close(double actual, double expected, const char* message)
{
    if (!std::isfinite(actual) || !std::isfinite(expected)
        || std::abs(actual - expected) > 2.0e-11 * std::max(1.0, std::abs(expected))) {
        std::cerr << message << " actual=" << actual << " expected=" << expected << '\n';
        throw std::runtime_error(message);
    }
}

struct ConstantGeometryEos {
    ARCH_INLINE double get_pressure(const FluidVector&, const double*) const { return 5.0; }
    ARCH_INLINE double get_temperature(double, double, const double*) const { return 100.0; }
    ARCH_INLINE void evaluate_state(eos_state_t&) const {}
};

__global__ void independent_metrics_kernel(
    const CurvilinearMetricCases::MeasureCase* cases, int count, double* error)
{
    // Runtime-uploaded inputs prevent the device compiler from replacing the
    // entire fixed-data calculation with a compile-time constant.
    double maximum = 0.0;
    for (int index = 0; index < count; ++index)
        maximum = std::max(maximum, CurvilinearMetricCases::conditioning_error(cases[index]));
    *error = maximum;
}

void independent_metrics()
{
    DeviceBuffer<double> error(1);
    const std::vector<CurvilinearMetricCases::MeasureCase> inputs(
        std::begin(CurvilinearMetricCases::cases), std::end(CurvilinearMetricCases::cases));
    DeviceBuffer<CurvilinearMetricCases::MeasureCase> cases(inputs.size());
    cases.upload(inputs);
    independent_metrics_kernel<<<1, 1>>>(cases.get(), static_cast<int>(inputs.size()), error.get());
    check(cudaGetLastError());
    const double device_error = error.download()[0];
    const double host_error = CurvilinearMetricCases::conditioning_error();
    require(std::isfinite(device_error) && device_error <= 2.e-12,
            "independent CUDA thin-shell/polar measures");
    require(std::isfinite(host_error) && host_error <= 2.e-12,
            "independent Host thin-shell/polar measures");
    std::cout << "INDEPENDENT_METRIC_MAX_RELATIVE_ERROR host=" << host_error
              << " device=" << device_error << '\n';
}

void analytic_geometry_examples()
{
    // Hand-derived examples: rho=2, velocity=(2,3,4), p=5,
    // r=2, theta=pi/4, dt=.1, nu=.03. Native velocity components are
    // constant in neighbouring cells. Viscous source is mu*sum C_d(C_d(v));
    // spherical 2D is polar, spherical 3D includes radial/theta cot coupling.
    // Spherical 1D/3D sources use <1/r> = 6/13 on the shell [1,3].
    // The volume-weighted source is 12/13 of its midpoint value (1/r_mid = 1/2).
    struct Example {
        GridMetrics::Geometry geometry;
        int dimension;
        std::array<double, 3> hydro;
        std::array<double, 3> viscous;
        double volume;
    };
    constexpr double pi = 3.14159265358979323846;
    const Example examples[]{
        {GridMetrics::Geometry::Cylindrical, 1, {.25, 0, 0}, {-.003, 0, 0}, 4.0},
        {GridMetrics::Geometry::Cylindrical, 2, {1.15, -.6, 0}, {-.003, -.0045, 0}, 2*pi},
        {GridMetrics::Geometry::Cylindrical, 3, {1.85, 0, -.8}, {-.003, 0, -.006}, pi/2},
        {GridMetrics::Geometry::Spherical, 1, {6.0/13, 0, 0}, {-.006*12/13, 0, 0}, 26.0/3},
        {GridMetrics::Geometry::Spherical, 2, {1.15, -.6, 0}, {-.003, -.0045, 0}, 2*pi},
        {GridMetrics::Geometry::Spherical, 3, {36.0/13, 15.0/13, -24.0/13}, {-.0105*12/13, -.012*12/13, -.012*12/13}, 13.0/6},
    };
    DiffFlux::DiffusionConfigView config{};
    config.use_diffusion = config.use_viscous_diffusion = true;
    config.nu_visc = .03;
    for (const auto& example : examples) {
        GridMetrics::GeometryView grid{};
        grid.geometry = example.geometry;
        grid.dim = example.dimension;
        grid.dx1 = 2.0;
        grid.dx2 = pi/2;
        grid.dx3 = .25;
        grid.x1_min = 1.0;
        const FluidVector state{2.0, 4.0, 6.0, 8.0, 100.0};
        FluidVector hydro{}, viscous{};
        TimeIntegration::add_geometric_source_cell(
            state, nullptr, ConstantGeometryEos{}, grid, 0, 0, .1, hydro);
        const auto result = DiffFlux::evaluate_geometric_diffusion_cell(
            state, nullptr, ConstantGeometryEos{}, SpeciesPODView{}, config,
            grid, 0, 0, 0, .1, nullptr, nullptr, viscous,
            [&state](int) { return state; });
        require(result.valid && result.active, "frozen geometry coefficient unexpectedly inactive");
        const double hydro_values[]{hydro.mom_u, hydro.mom_v, hydro.mom_w};
        const double viscous_values[]{viscous.mom_u, viscous.mom_v, viscous.mom_w};
        for (int axis = 0; axis < 3; ++axis) {
            close(hydro_values[axis], example.hydro[axis], "frozen main hydro geometry");
            close(viscous_values[axis], example.viscous[axis], "analytic covariant viscous source");
        }
        close(GridMetrics::CellVolume(grid, 0, 0, 0), example.volume, "frozen main metric volume");
        close(hydro.rho, 0.0, "geometry must not source mass");
        close(hydro.eng, 0.0, "geometry must not source energy");
    }
}

__global__ void metrics_kernel(arch::cuda::DeviceGridView grid, double* metrics)
{
    const int linear = blockIdx.x * blockDim.x + threadIdx.x;
    if (linear >= grid.active_cell_count()) return;
    const int ni = grid.ie - grid.is;
    const int nj = grid.je - grid.js;
    const int i = grid.is + linear % ni;
    const int j = grid.js + (linear / ni) % nj;
    const int k = grid.ks + linear / (ni * nj);
    const int cell = grid.active_cell(linear);
    const auto geometry = arch::cuda::make_grid_geometry_view(grid);
    metrics[cell] = GridMetrics::CellVolume(geometry, i, j, k);
    for (int direction = 0; direction < grid.dim; ++direction) {
        metrics[(1 + direction) * grid.total_size + cell] =
            GridMetrics::FaceArea(geometry, direction, i, j, k, false);
        metrics[(4 + direction) * grid.total_size + cell] =
            GridMetrics::FaceArea(geometry, direction, i, j, k, true);
    }
}

void cache_metrics(arch::cuda::DeviceGridView& grid, DeviceBuffer<double>& metrics)
{
    metrics_kernel<<<(grid.active_cell_count() + 127) / 128, 128>>>(grid, metrics.get());
    check(cudaGetLastError());
    grid.cell_volume = metrics.get();
    for (int direction = 0; direction < 3; ++direction) {
        grid.face_area_lower[direction] = metrics.get() + (1 + direction) * grid.total_size;
        grid.face_area_upper[direction] = metrics.get() + (4 + direction) * grid.total_size;
    }
}

std::vector<double> device_diffusion_operator(
    const DeviceState& input, DeviceState& output, DeviceState& faces,
    arch::cuda::DeviceGridView grid, IdealGasView eos, SpeciesPODView species,
    DiffFlux::DiffusionConfigView config, DeviceBuffer<int>& status,
    arch::cuda::SpeciesWorkspaceView workspace = {})
{
    check(arch::cuda::clear_hydro_buffer(output.view, nullptr));
    for (int direction = 0; direction < grid.dim; ++direction) {
        check(arch::cuda::clear_hydro_buffer(faces.view, nullptr));
        const int count = arch::cuda::detail::diffusion_face_count(grid, direction);
        arch::cuda::detail::diffusion_face_kernel
            <<<arch::cuda::detail::species_launch_blocks(count, workspace),
               arch::cuda::detail::species_launch_threads(workspace)>>>(
                input.view, faces.view, grid, eos, species,
                config, direction, status.get(), workspace);
        check(cudaGetLastError());
        check(arch::cuda::launch_hydro_divergence(faces.view, output.view, grid,
                                                1.0, direction, nullptr));
    }
    arch::cuda::detail::diffusion_geometric_source_kernel
        <<<arch::cuda::detail::species_launch_blocks(grid.active_cell_count(), workspace),
           arch::cuda::detail::species_launch_threads(workspace)>>>(
            input.view, output.view, grid, eos, species, config, status.get(), workspace);
    check(cudaGetLastError());
    auto actual = output.storage.download();
    require(status.download()[0] == 0, "geometric diffusion status rejected valid state");
    return actual;
}

void run_case(const char* geometry_name, int dimension, int species_count = 2)
{
    Grid grid(amr::MAX_NG, 1.0, 2.0, 0.25, 1.25, 0.0, 1.0);
    grid.dim = dimension;
    grid.geometry = geometry_name;
    grid.InitializeTopology();
    const int cells = grid.GetTotalSize();
    auto device_grid = arch::cuda::make_device_grid_view(grid);
    DeviceBuffer<double> metrics(7 * cells);
    cache_metrics(device_grid, metrics);
    const auto measured_metrics = metrics.download();
    for (int k = grid.Ks(); k < grid.Ke(); ++k)
        for (int j = grid.Js(); j < grid.Je(); ++j)
            for (int i = grid.Is(); i < grid.Ie(); ++i) {
                const int cell = grid.GetIndex(i, j, k);
                close(measured_metrics[cell], GridMetrics::CellVolume(grid, i, j, k), "cell volume parity");
                for (int direction = 0; direction < dimension; ++direction) {
                    close(measured_metrics[(1 + direction) * cells + cell],
                          GridMetrics::FaceArea(grid, direction, i, j, k, false), "lower area parity");
                    close(measured_metrics[(4 + direction) * cells + cell],
                          GridMetrics::FaceArea(grid, direction, i, j, k, true), "upper area parity");
                }
            }

    SpeciesManager species;
    std::vector<double> properties(4 * species_count);
    for (int sp = 0; sp < species_count; ++sp) {
        const double mass = sp == 0 ? 1.0 : 2.0 * (sp + 1);
        const double charge = sp + 1.0;
        const double gamma = sp == 0 ? 1.4 : 1.5;
        const double cv = sp == 0 ? 3.0 : 4.0;
        species.add_species("species_" + std::to_string(sp), mass, charge, gamma, cv);
        properties[sp] = mass;
        properties[species_count + sp] = charge;
        properties[2 * species_count + sp] = gamma;
        properties[3 * species_count + sp] = cv;
    }
    IdealGas host_eos(1.4, species);
    DeviceBuffer<double> species_data(4 * species_count);
    species_data.upload(properties);
    const SpeciesPODView device_species{
        species_data.get(), species_data.get() + species_count, species_data.get() + 2 * species_count,
        species_data.get() + 3 * species_count, species_count};
    const IdealGasView device_eos{device_species, 1.4};
    int device = 0;
    cudaDeviceProp device_properties{};
    check(cudaGetDevice(&device));
    check(cudaGetDeviceProperties(&device_properties, device));
    const int lanes = device_properties.warpSize;
    require(lanes > 0, "invalid CUDA warp size");
    const bool dynamic = species_count > arch::cuda::kLocalSpeciesScratchCapacity;
    const std::size_t scratch_size = dynamic ? 5ULL * lanes * species_count : 1;
    DeviceBuffer<double> scratch(scratch_size);
    const arch::cuda::SpeciesWorkspaceView species_workspace = dynamic
        ? arch::cuda::SpeciesWorkspaceView{scratch.get(), scratch_size, lanes, species_count, 5}
        : arch::cuda::SpeciesWorkspaceView{};
    FluidState state;
    initialize(state, cells, species_count);
    for (int k = 0; k < grid.GetTotalZ(); ++k)
        for (int j = 0; j < grid.GetTotalY(); ++j)
            for (int i = 0; i < grid.GetTotalX(); ++i) {
                const int cell = grid.GetIndex(i, j, k);
                const double x = grid.GetCellCenterX(i);
                const double y = grid.GetCellCenterY(j);
                const double z = grid.GetCellCenterZ(k);
                const double rho = 1.0 + 0.01 * x;
                state.set(cell, {rho, rho * (0.1 + 0.02 * x * x),
                                 rho * (0.05 + 0.01 * y * y), rho * (0.02 + 0.01 * z * z),
                                 rho * (12.0 + 0.1 * x * x + 0.1 * y * y + 0.1 * z * z)});
                state.X(0, cell) = 0.3 + 0.01 * x + 0.01 * y + 0.01 * z;
                const double remainder = (1.0 - state.X(0, cell)) / (species_count - 1);
                double sum = state.X(0, cell);
                for (int sp = 1; sp + 1 < species_count; ++sp) {
                    state.X(sp, cell) = remainder;
                    sum += remainder;
                }
                state.X(species_count - 1, cell) = 1.0 - sum;
            }
    DeviceState input(cells, species_count), output(cells, species_count), face_flux(cells, species_count);
    input.storage.upload(pack(state));
    std::vector<FluidVector> host_delta(cells);
    TimeIntegration::add_geometric_sources(host_delta, state, host_eos, grid, 0.01);
    check(arch::cuda::launch_hydro_sources(input.view, output.view, device_grid,
                                                   device_eos, 0.01, nullptr, species_workspace));
    auto actual = output.storage.download();
    double radial_source = 0.0;
    for (int linear = 0; linear < device_grid.active_cell_count(); ++linear) {
        const int cell = device_grid.active_cell(linear);
        close(actual[cells + cell], host_delta[cell].mom_u, "hydro radial source parity");
        close(actual[2 * cells + cell], host_delta[cell].mom_v, "hydro second source parity");
        close(actual[3 * cells + cell], host_delta[cell].mom_w, "hydro third source parity");
        radial_source += std::abs(actual[cells + cell]);
    }
    if (grid.geometry != "cartesian")
        require(radial_source > 0.0, "curvilinear hydro source was never exercised");

    // Exercise the same production source launch with external gravity on every
    // geometry/dimension. Independent momentum/work oracle; no copied CPU kernel.
    const Physical::Gravity::ExternalGravityView gravity{.2, -.3, .4, true};
    check(arch::cuda::clear_hydro_buffer(output.view, nullptr));
    check(arch::cuda::launch_hydro_sources(input.view, output.view, device_grid,
        device_eos, .01, nullptr, species_workspace, gravity));
    actual = output.storage.download();
    for (int linear = 0; linear < device_grid.active_cell_count(); ++linear) {
        const int cell = device_grid.active_cell(linear);
        close(actual[cells + cell], host_delta[cell].mom_u + .002 * state.rho[cell],
              "external radial acceleration");
        close(actual[2*cells + cell], host_delta[cell].mom_v - .003 * state.rho[cell],
              "external second acceleration");
        close(actual[3*cells + cell], host_delta[cell].mom_w + .004 * state.rho[cell],
              "external third acceleration");
        close(actual[4*cells + cell], .01 * (.2 * state.mom_u[cell]
            - .3 * state.mom_v[cell] + .4 * state.mom_w[cell]), "external gravity work");
        close(actual[cell], 0, "external gravity must not source mass");
    }

    // Compare actual transport faces, including every passive species, with
    // the ordinary CPU flux entry point. One warp forces multiple grid-stride
    // iterations in the large-species multidimensional cases.
    for (int direction = 0; direction < dimension; ++direction) {
        std::vector<FluidVector> host_flux(cells);
        std::vector<double> host_species_flux(static_cast<std::size_t>(cells) * species_count);
        FluxHLL<PCMReconstruction>::compute_fluxes(
            state, host_eos, grid, host_flux, host_species_flux, direction, 0.0);
        check(arch::cuda::launch_hydro_faces<arch::cuda::CudaPcmReconstruction, arch::cuda::CudaHllFlux>(
            input.view, face_flux.view, device_grid, device_eos, direction, 0.0, nullptr, species_workspace));
        actual = face_flux.storage.download();
        const int faces = arch::cuda::detail::diffusion_face_count(device_grid, direction);
        for (int linear = 0; linear < faces; ++linear) {
            const int cell = arch::cuda::detail::diffusion_face_cell(device_grid, direction, linear);
            const auto& flux = host_flux[cell];
            const double fields[]{flux.rho, flux.mom_u, flux.mom_v, flux.mom_w, flux.eng};
            for (int field = 0; field < 5; ++field)
                close(actual[field * cells + cell], fields[field], "hydro face parity");
            for (int sp = 0; sp < species_count; ++sp)
                close(actual[(6 + sp) * cells + cell], host_species_flux[sp * cells + cell],
                      "hydro passive species face parity");
        }
    }

    SimConfig config{};
    auto& diffusion = config.physics.diffusion;
    diffusion.use_diffusion = diffusion.use_thermal_diffusion = true;
    diffusion.use_viscous_diffusion = diffusion.use_species_diffusion = true;
    diffusion.nu_visc = 0.03;
    diffusion.alpha_therm = 0.1;
    diffusion.D_spec = 0.02;
    const auto device_config = DiffFlux::make_diffusion_config_view(config);
    FluidState expected;
    initialize(expected, cells, species_count);
    DiffFlux::compute_diffusion_operator(state, expected, host_eos, grid, config);
    DeviceBuffer<int> status(1);
    actual = device_diffusion_operator(input, output, face_flux, device_grid,
        device_eos, device_species, device_config, status, species_workspace);
    const auto host_operator = pack(expected);
    for (int field = 0; field < 6 + species_count; ++field) {
        if (field == 5) continue;
        for (int linear = 0; linear < device_grid.active_cell_count(); ++linear) {
            const int cell = device_grid.active_cell(linear);
            close(actual[field * cells + cell], host_operator[field * cells + cell],
                  "curvilinear diffusion operator parity");
        }
    }
    DeviceBuffer<double> candidates(device_grid.active_cell_count()), timestep(1);
    arch::cuda::DiffusionWorkspaceView workspace{face_flux.view, candidates.get(), timestep.get(), status.get(), species_workspace};
    const auto launch = arch::cuda::launch_raw_diffusion_dt(
        input.view, device_eos, device_species, device_grid, device_config, workspace, nullptr);
    check(launch.error);
    require(status.download()[0] == 0, "geometric diffusion timestep rejected valid state");
    close(timestep.download()[0], DiffFlux::adaptive_dt_diff(state, host_eos, grid, config, 1.0),
          "curvilinear diffusion timestep parity");
    arch::cuda::CudaHydroWorkspaceView hydro_workspace{
        face_flux.view, output.view, candidates.get(), timestep.get(), status.get(), species_workspace};
    check(arch::cuda::launch_compute_hydro_dt(input.view, device_grid, device_eos, 0.4,
                                             hydro_workspace, nullptr));
    require(status.download()[0] == static_cast<int>(arch::reduction::ReductionStatus::Ok),
            "hydro timestep rejected valid composition");
    close(timestep.download()[0], adaptive_dt(state, host_eos, grid, 0.4), "hydro CFL parity");
    if (dynamic) {
        auto invalid = species_workspace;
        --invalid.capacity;
        require(!arch::cuda::valid_species_workspace(invalid, species_count, 5),
                "truncated species workspace accepted");
        invalid = species_workspace;
        --invalid.species;
        require(!arch::cuda::valid_species_workspace(invalid, species_count, 5),
                "wrong network workspace accepted");
        invalid = species_workspace;
        invalid.arrays = 3;
        require(!arch::cuda::valid_species_workspace(invalid, species_count, 5),
                "narrow diffusion workspace accepted");
        invalid = species_workspace;
        invalid.lanes = 0;
        require(!arch::cuda::valid_species_workspace(invalid, species_count, 5),
                "zero-lane workspace accepted");
        require(arch::cuda::launch_hydro_faces<arch::cuda::CudaPcmReconstruction, arch::cuda::CudaHllFlux>(
            input.view, face_flux.view, device_grid, device_eos, 0, 0.0, nullptr)
                == cudaErrorInvalidValue, "large hydro accepted missing species workspace");
        workspace.species_workspace = {};
        require(arch::cuda::launch_raw_diffusion_dt(input.view, device_eos, device_species,
            device_grid, device_config, workspace, nullptr).error == cudaErrorInvalidValue,
            "large diffusion accepted missing species workspace");
    }
    std::cout << "CURVILINEAR_GEOMETRY_SMOKE_PASS geometry=" << geometry_name
              << " dim=" << dimension << " species=" << species_count << '\n';
}

void scalar_diffusion_convergence(const char* name, int dimension, bool thermal)
{
    // Manufactured X or T=.4+.001*r^4+.01*cos(q2)+.02*cos(q3),
    // omitting inactive coordinates, rho=1 and constant D or alpha*cv. Its continuum
    // Laplacian below is independent of either discrete backend. Shrinking
    // patches keep the sampled cell at (r,q2,q3)=(2,1,.7); this is spatial
    // operator convergence, not a complete time-dependent AMR simulation.
    constexpr double heat_capacity = 3.0;
    SpeciesManager species;
    species.add_species("first", 1., 1., 1.4, heat_capacity);
    species.add_species("second", 4., 2., 1.4, heat_capacity);
    IdealGas host_eos(1.4, species);
    DeviceBuffer<double> properties(8);
    properties.upload({1., 4., 1., 2., 1.4, 1.4, heat_capacity, heat_capacity});
    const SpeciesPODView device_species{properties.get(), properties.get()+2,
        properties.get()+4, properties.get()+6, 2};
    const IdealGasView device_eos{device_species, 1.4};
    SimConfig config{};
    config.physics.diffusion.use_diffusion = true;
    config.physics.diffusion.use_species_diffusion = !thermal;
    config.physics.diffusion.use_thermal_diffusion = thermal;
    config.physics.diffusion.D_spec = .02;
    config.physics.diffusion.alpha_therm = .02;
    double previous_host = 0., previous_device = 0.;
    for (double spacing : {.05, .025, .0125}) {
        const double x = 2. - (amr::BLOCK_NX/2 + .5) * spacing;
        const double y = 1. - (amr::BLOCK_NY/2 + .5) * spacing;
        const double z = .7 - (amr::BLOCK_NZ/2 + .5) * spacing;
        Grid grid(amr::MAX_NG, x, x+amr::BLOCK_NX*spacing,
            y, y+amr::BLOCK_NY*spacing, z, z+amr::BLOCK_NZ*spacing);
        grid.dim = dimension; grid.geometry = name; grid.InitializeTopology();
        const int cells = grid.GetTotalSize();
        FluidState state, host_delta;
        initialize(state, cells); initialize(host_delta, cells);
        for (int k=0; k<grid.GetTotalZ(); ++k)
            for (int j=0; j<grid.GetTotalY(); ++j)
                for (int i=0; i<grid.GetTotalX(); ++i) {
                    const int cell = grid.GetIndex(i,j,k);
                    double fraction = .4 + .001*std::pow(grid.GetCellCenterX(i), 4);
                    if (dimension >= 2) fraction += .01*std::cos(grid.GetCellCenterY(j));
                    if (dimension == 3) fraction += .02*std::cos(grid.GetCellCenterZ(k));
                    require(fraction > 0. && fraction < 1., "invalid manufactured composition");
                    state.set(cell, {1., 0., 0., 0., thermal ? heat_capacity*fraction : 30.});
                    state.X(0,cell) = thermal ? .5 : fraction;
                    state.X(1,cell) = 1.-state.X(0,cell);
                }
        DiffFlux::compute_diffusion_operator(state, host_delta, host_eos, grid, config);
        auto device_grid = arch::cuda::make_device_grid_view(grid);
        DeviceBuffer<double> metrics(7*cells);
        cache_metrics(device_grid, metrics);
        DeviceState input(cells), output(cells), faces(cells);
        input.storage.upload(pack(state));
        DeviceBuffer<int> status(1);
        const auto actual = device_diffusion_operator(input, output, faces, device_grid,
            device_eos, device_species, DiffFlux::make_diffusion_config_view(config), status);
        const int i = grid.Is()+amr::BLOCK_NX/2;
        const int j = dimension >= 2 ? grid.Js()+amr::BLOCK_NY/2 : 0;
        const int k = dimension == 3 ? grid.Ks()+amr::BLOCK_NZ/2 : 0;
        const int cell = grid.GetIndex(i,j,k);
        const double radius = grid.GetCellCenterX(i);
        const double theta = grid.GetCellCenterY(j), phi = grid.GetCellCenterZ(k);
        double laplacian = 0.;
        if (grid.geometry == "cartesian") {
            laplacian = .012*radius*radius;
            if (dimension >= 2) laplacian -= .01*std::cos(theta);
            if (dimension == 3) laplacian -= .02*std::cos(phi);
        } else if (grid.geometry == "cylindrical" || dimension == 2) {
            // Cylindrical 3D is (r,z,phi); spherical 2D is polar (r,phi).
            laplacian = .016*radius*radius;
            if (dimension == 2) laplacian -= .01*std::cos(theta)/(radius*radius);
            if (dimension == 3)
                laplacian -= .01*std::cos(theta) + .02*std::cos(phi)/(radius*radius);
        } else {
            laplacian = .020*radius*radius;
            if (dimension == 3)
                laplacian -= (.02*std::cos(theta)
                    + .02*std::cos(phi)/std::pow(std::sin(theta), 2))/(radius*radius);
        }
        const double expected = (thermal ? config.physics.diffusion.alpha_therm*heat_capacity
                                         : config.physics.diffusion.D_spec)*laplacian;
        const double host_value = thermal ? host_delta.eng[cell] : host_delta.X(0,cell);
        const double device_value = actual[(thermal ? 4 : 6)*cells+cell];
        close(device_value, host_value, "manufactured diffusion backend parity");
        if (!thermal) close(actual[7*cells+cell], -device_value, "manufactured species conservation");
        const double host_error = std::abs(host_value-expected);
        const double device_error = std::abs(device_value-expected);
        require(std::isfinite(host_error) && std::isfinite(device_error), "nonfinite convergence error");
        if (previous_host > 0.) {
            // Second-order consistency predicts a factor four; require order
            // greater than log2(3.5), independently on CPU and CUDA.
            require(previous_host >= 3.5*host_error, "CPU diffusion lost second-order consistency");
            require(previous_device >= 3.5*device_error, "CUDA diffusion lost second-order consistency");
        }
        previous_host = host_error; previous_device = device_error;
        std::cout << (thermal ? "THERMAL_SPATIAL_CONVERGENCE" : "DIFFUSION_SPATIAL_CONVERGENCE")
                  << " geometry=" << name << " dim=" << dimension
                  << " h=" << spacing << " host_error=" << host_error
                  << " device_error=" << device_error << '\n';
    }
    require(previous_host <= 1.e-6 && previous_device <= 1.e-6,
            "scalar analytic derivative budget exceeded");
}

void viscous_diffusion_convergence()
{
    DeviceBuffer<double> properties(4);
    properties.upload({1., 1., 1.4, 3.});
    const SpeciesPODView species{properties.get(), properties.get()+1,
        properties.get()+2, properties.get()+3, 1};
    const IdealGasView eos{species, 1.4};
    DiffFlux::DiffusionConfigView config{};
    config.use_diffusion = config.use_viscous_diffusion = true;
    config.nu_visc = ViscousGeometryCases::viscosity;
    const auto evaluate = [&](const FluidState& state, const Grid& grid) {
        const int cells = grid.GetTotalSize();
        auto device_grid = arch::cuda::make_device_grid_view(grid);
        DeviceBuffer<double> metrics(7*cells);
        cache_metrics(device_grid, metrics);
        DeviceState input(cells, 1), output(cells, 1), faces(cells, 1);
        input.storage.upload(pack(state));
        DeviceBuffer<int> status(1);
        const auto actual = device_diffusion_operator(input, output, faces, device_grid,
            eos, species, config, status);
        DeviceBuffer<double> candidates(device_grid.active_cell_count()), timestep(1);
        arch::cuda::DiffusionWorkspaceView workspace{faces.view, candidates.get(), timestep.get(), status.get()};
        check(arch::cuda::launch_raw_diffusion_dt(input.view, eos, species, device_grid,
                                                config, workspace, nullptr).error);
        require(status.download()[0] == 0, "radial diffusion timestep rejected valid state");
        ViscousGeometryCases::Evaluation result{};
        result.derivative.resize(cells);
        for (int cell=0; cell<cells; ++cell)
            result.derivative[cell] = {actual[cell], actual[cells+cell], actual[2*cells+cell],
                                      actual[3*cells+cell], actual[4*cells+cell]};
        result.raw_dt = timestep.download()[0];
        return result;
    };
    ViscousGeometryCases::convergence("cuda", evaluate);
    ViscousGeometryCases::radial_origin("cuda", evaluate);
    ViscousGeometryCases::density_stability("cuda", evaluate);
}

} // namespace

int main()
{
    int count = 0;
    const auto probe = cudaGetDeviceCount(&count);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && count == 0)) {
        static_cast<void>(cudaGetLastError());
        std::cout << "SKIP: CUDA runtime device unavailable\n";
        return 77;
    }
    try {
        check(probe);
        independent_metrics();
        analytic_geometry_examples();
        viscous_diffusion_convergence();
        for (const char* geometry : {"cylindrical", "spherical"})
            for (int dimension = 1; dimension <= 3; ++dimension)
                run_case(geometry, dimension);
        run_case("cartesian", 2, 41);
        run_case("spherical", 3, 41);
        for (const char* geometry : {"cartesian", "cylindrical", "spherical"})
            for (int dimension = 1; dimension <= 3; ++dimension)
                for (bool thermal : {false, true})
                    scalar_diffusion_convergence(geometry, dimension, thermal);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
