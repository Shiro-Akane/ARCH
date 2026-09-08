/**
 * @file test_tabular_free_energy_owner.cu
 * @brief Check thermodynamics through tabular free-energy device owners.
 *
 * Manufactured 3D/4D fields exercise values, derivatives and failure behavior
 * through the same EOS views used by the production kernels.
 */
#include "cuda/microphysics/helm_eos_loader.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using tabular_eos::FreeEnergyResult;
using tabular_eos::FreeEnergyStatus;
using tabular_eos::ThermodynamicState;

constexpr double kGasConstant = 2.0;
constexpr double kHeatCapacity = 3.0;

void cuda_check(cudaError_t status, const char* operation)
{
    if (status != cudaSuccess) {
        throw std::runtime_error(
            std::string(operation) + ": " + cudaGetErrorString(status));
    }
}

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void close(double actual, double expected, const std::string& field)
{
    require(std::isfinite(actual), field + " is not finite");
    const double relative = std::abs(actual - expected)
        / std::max(1.0, std::abs(expected));
    require(relative <= 2.0e-13, field + " differs from host");
}

bool all_nan(const ThermodynamicState& state)
{
    const double values[]{
        state.pressure, state.energy, state.cv, state.dp_drho_e,
        state.dp_de_rho, state.dp_dT, state.sound_speed, state.gamma1};
    for (double value : values) {
        if (!std::isnan(value)) return false;
    }
    return true;
}

void compare_state(const ThermodynamicState& actual,
                   const ThermodynamicState& expected,
                   const std::string& prefix)
{
    close(actual.pressure, expected.pressure, prefix + ".pressure");
    close(actual.energy, expected.energy, prefix + ".energy");
    close(actual.cv, expected.cv, prefix + ".cv");
    close(actual.dp_drho_e, expected.dp_drho_e,
          prefix + ".dp_drho_e");
    close(actual.dp_de_rho, expected.dp_de_rho,
          prefix + ".dp_de_rho");
    close(actual.dp_dT, expected.dp_dT, prefix + ".dp_dT");
    close(actual.sound_speed, expected.sound_speed,
          prefix + ".sound_speed");
    close(actual.gamma1, expected.gamma1, prefix + ".gamma1");
}

struct Probe {
    FreeEnergyResult result{};
    ThermodynamicState public_state{};
};

static_assert(std::is_trivially_copyable_v<Probe>);

__global__ void probe3_kernel(Tabular3DEOSView view, double rho,
                              double temperature, double composition,
                              Probe* output)
{
    output->result = view.free_energy_result(rho, temperature, composition);
    output->public_state =
        view.free_energy_state(rho, temperature, composition);
}

__global__ void probe4_kernel(Tabular4DEOSView view, double rho,
                              double temperature, double A, double Z,
                              Probe* output)
{
    output->result = view.free_energy_result(rho, temperature, A, Z);
    output->public_state = view.free_energy_state(rho, temperature, A, Z);
}

class DeviceProbe {
public:
    DeviceProbe()
    {
        cuda_check(cudaMalloc(reinterpret_cast<void**>(&pointer_),
                              sizeof(Probe)),
                   "allocate free-energy probe");
    }

    ~DeviceProbe()
    {
        if (pointer_ != nullptr) static_cast<void>(cudaFree(pointer_));
    }

    DeviceProbe(const DeviceProbe&) = delete;
    DeviceProbe& operator=(const DeviceProbe&) = delete;

    Probe* get() const noexcept { return pointer_; }

private:
    Probe* pointer_ = nullptr;
};

Probe download_probe(DeviceProbe& storage, cudaStream_t stream)
{
    Probe result{};
    cuda_check(cudaMemcpyAsync(
                   &result, storage.get(), sizeof(result),
                   cudaMemcpyDeviceToHost, stream),
               "download free-energy probe");
    cuda_check(cudaStreamSynchronize(stream),
               "synchronize free-energy probe");
    return result;
}

Probe run_probe(Tabular3DEOSView view, double rho, double temperature,
                double composition, cudaStream_t stream)
{
    DeviceProbe storage;
    probe3_kernel<<<1, 1, 0, stream>>>(
        view, rho, temperature, composition, storage.get());
    cuda_check(cudaGetLastError(), "launch Tabular3 free-energy probe");
    return download_probe(storage, stream);
}

Probe run_probe(Tabular4DEOSView view, double rho, double temperature,
                double A, double Z, cudaStream_t stream)
{
    DeviceProbe storage;
    probe4_kernel<<<1, 1, 0, stream>>>(
        view, rho, temperature, A, Z, storage.get());
    cuda_check(cudaGetLastError(), "launch Tabular4 free-energy probe");
    return download_probe(storage, stream);
}

std::array<std::vector<double>, tabular_eos::FieldCount>
make_ideal_gas_fields(int composition_count)
{
    constexpr int n_rho = 3;
    constexpr int n_temperature = 3;
    const std::size_t extent = static_cast<std::size_t>(n_rho)
        * n_temperature * composition_count;
    std::array<std::vector<double>, tabular_eos::FieldCount> fields;
    for (auto& field : fields) field.resize(extent, 0.0);

    const double ln_10 = std::log(10.0);
    for (int i = 0; i < n_rho; ++i) {
        const double x = ln_10 * i;
        for (int j = 0; j < n_temperature; ++j) {
            const double y = ln_10 * j;
            const double temperature = std::exp(y);
            // a(rho,T) = T * (R ln(rho) - cv ln(T) + cv).
            const double potential = kGasConstant * x
                - kHeatCapacity * y + kHeatCapacity;
            for (int composition = 0;
                 composition < composition_count; ++composition) {
                const std::size_t index =
                    (static_cast<std::size_t>(i) * n_temperature + j)
                        * composition_count
                    + composition;
                fields[tabular_eos::F][index] = temperature * potential;
                fields[tabular_eos::Fx][index] =
                    kGasConstant * temperature;
                fields[tabular_eos::Fy][index] =
                    temperature * (potential - kHeatCapacity);
                fields[tabular_eos::Fxy][index] =
                    kGasConstant * temperature;
                fields[tabular_eos::Fyy][index] =
                    temperature * (potential - 2.0 * kHeatCapacity);
                fields[tabular_eos::Fxyy][index] =
                    kGasConstant * temperature;
            }
        }
    }
    return fields;
}

template <class HostView>
void bind_free_energy_fields(
    HostView& view,
    const std::array<std::vector<double>, tabular_eos::FieldCount>& fields)
{
    view.uses_free_energy = true;
    for (int field = 0; field < tabular_eos::FieldCount; ++field) {
        view.free_energy_fields[field] = fields[field].data();
        view.free_energy_extents[field] = fields[field].size();
    }
}

Tabular3DEOSHostView make_host3(
    const SpeciesManager& species,
    const std::array<std::vector<double>, tabular_eos::FieldCount>& fields)
{
    Tabular3DEOSHostView view{};
    view.n_rho = view.n_T = view.n_X = 3;
    view.log_rho_min = view.log_T_min = 0.0;
    view.log_rho_max = view.log_T_max = 2.0;
    view.dlog_rho = view.dlog_T = 1.0;
    view.X_min = 0.0;
    view.X_max = 1.0;
    view.dX = 0.5;
    view.target_species_id = 0;
    view.specs = species.get_host_view();
    bind_free_energy_fields(view, fields);
    return view;
}

Tabular4DEOSHostView make_host4(
    const SpeciesManager& species,
    const std::array<std::vector<double>, tabular_eos::FieldCount>& fields)
{
    Tabular4DEOSHostView view{};
    view.n_rho = view.n_T = view.n_A = view.n_Z = 3;
    view.log_rho_min = view.log_T_min = 0.0;
    view.log_rho_max = view.log_T_max = 2.0;
    view.dlog_rho = view.dlog_T = 1.0;
    view.A_min = 1.0;
    view.A_max = 2.0;
    view.dA = 0.5;
    view.Z_min = 0.5;
    view.Z_max = 1.0;
    view.dZ = 0.25;
    view.specs = species.get_host_view();
    bind_free_energy_fields(view, fields);
    return view;
}

void require_host_failure(const FreeEnergyResult& result,
                          FreeEnergyStatus expected,
                          const std::string& prefix)
{
    require(result.status == expected, prefix + " host status drifted");
    require(all_nan(result.state), prefix + " host result is not NaN");
}

void require_device_failure(const Probe& probe, FreeEnergyStatus expected,
                            const std::string& prefix)
{
    require(probe.result.status == expected,
            prefix + " device status drifted");
    require(all_nan(probe.result.state),
            prefix + " device result is not NaN");
    require(all_nan(probe.public_state),
            prefix + " device public state is not NaN");
}

void test_free_energy_owners(cudaStream_t stream)
{
    SpeciesManager species;
    species.add_species(
        "ion", 1.0, 0.5, 5.0 / 3.0, kHeatCapacity);
    constexpr double rho = 1.0;
    constexpr double temperature = 1.0;
    const ThermodynamicState analytic{
        2.0, 3.0, 3.0, 2.0, 2.0 / 3.0, 2.0,
        std::sqrt(10.0 / 3.0), 5.0 / 3.0};

    auto fields3 = make_ideal_gas_fields(3);
    const auto host3 = make_host3(species, fields3);
    const auto host3_result =
        host3.free_energy_result(rho, temperature, 0.25);
    require(host3_result.status == FreeEnergyStatus::success,
            "Tabular3 host free-energy status is not success");
    compare_state(host3_result.state, analytic, "Tabular3 analytic host");
    arch::cuda::Tabular3DEOSDeviceOwner owner3(host3, stream);
    const Tabular3DEOSView view3 = owner3.view();
    require(view3.uses_free_energy,
            "Tabular3 owner dropped free-energy mode");
    for (const double* pointer : view3.free_energy_fields)
        require(pointer != nullptr, "Tabular3 owner omitted a derivative field");
    const Probe device3 = run_probe(
        view3, rho, temperature, 0.25, stream);
    require(device3.result.status == host3_result.status,
            "Tabular3 device status differs from host");
    compare_state(device3.result.state, host3_result.state,
                  "Tabular3 device result");
    compare_state(device3.public_state, host3_result.state,
                  "Tabular3 device public state");

    auto invalid_fields3 = fields3;
    invalid_fields3[tabular_eos::Fyy] = invalid_fields3[tabular_eos::Fy];
    const auto invalid_host3 = make_host3(species, invalid_fields3);
    const auto invalid_result3 =
        invalid_host3.free_energy_result(rho, temperature, 0.25);
    require_host_failure(invalid_result3,
                         FreeEnergyStatus::invalid_heat_capacity,
                         "Tabular3 invalid free energy");
    arch::cuda::Tabular3DEOSDeviceOwner invalid_owner3(
        invalid_host3, stream);
    require_device_failure(
        run_probe(invalid_owner3.view(), rho, temperature, 0.25, stream),
        FreeEnergyStatus::invalid_heat_capacity,
        "Tabular3 invalid free energy");

    auto fields4 = make_ideal_gas_fields(9);
    const auto host4 = make_host4(species, fields4);
    const auto host4_result =
        host4.free_energy_result(rho, temperature, 1.25, 0.625);
    require(host4_result.status == FreeEnergyStatus::success,
            "Tabular4 host free-energy status is not success");
    compare_state(host4_result.state, analytic, "Tabular4 analytic host");
    arch::cuda::Tabular4DEOSDeviceOwner owner4(host4, stream);
    const Tabular4DEOSView view4 = owner4.view();
    require(view4.uses_free_energy,
            "Tabular4 owner dropped free-energy mode");
    for (const double* pointer : view4.free_energy_fields)
        require(pointer != nullptr, "Tabular4 owner omitted a derivative field");
    const Probe device4 = run_probe(
        view4, rho, temperature, 1.25, 0.625, stream);
    require(device4.result.status == host4_result.status,
            "Tabular4 device status differs from host");
    compare_state(device4.result.state, host4_result.state,
                  "Tabular4 device result");
    compare_state(device4.public_state, host4_result.state,
                  "Tabular4 device public state");

    auto invalid_fields4 = fields4;
    invalid_fields4[tabular_eos::Fyy] = invalid_fields4[tabular_eos::Fy];
    const auto invalid_host4 = make_host4(species, invalid_fields4);
    const auto invalid_result4 = invalid_host4.free_energy_result(
        rho, temperature, 1.25, 0.625);
    require_host_failure(invalid_result4,
                         FreeEnergyStatus::invalid_heat_capacity,
                         "Tabular4 invalid free energy");
    arch::cuda::Tabular4DEOSDeviceOwner invalid_owner4(
        invalid_host4, stream);
    require_device_failure(
        run_probe(invalid_owner4.view(), rho, temperature, 1.25, 0.625,
                  stream),
        FreeEnergyStatus::invalid_heat_capacity,
        "Tabular4 invalid free energy");
}

class StreamOwner {
public:
    StreamOwner() { cuda_check(cudaStreamCreate(&stream_), "create stream"); }
    ~StreamOwner()
    {
        if (stream_ != nullptr) {
            static_cast<void>(cudaStreamSynchronize(stream_));
            static_cast<void>(cudaStreamDestroy(stream_));
        }
    }
    StreamOwner(const StreamOwner&) = delete;
    StreamOwner& operator=(const StreamOwner&) = delete;
    cudaStream_t get() const noexcept { return stream_; }

private:
    cudaStream_t stream_ = nullptr;
};

} // namespace

int main()
{
    int device_count = 0;
    const cudaError_t probe = cudaGetDeviceCount(&device_count);
    if (probe != cudaSuccess || device_count == 0) {
        std::cout << "SKIP: CUDA runtime device unavailable\n";
        static_cast<void>(cudaGetLastError());
        return 77;
    }

    try {
        StreamOwner stream;
        test_free_energy_owners(stream.get());
        std::cout << "CUDA tabular free-energy owner passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
