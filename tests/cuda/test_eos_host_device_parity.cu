#include "physics/eos/IdealGas.h"

#include "cuda/microphysics/helm_eos_loader.h"
#include "physics/eos/HelmEos.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

void cuda_check(cudaError_t status, const char* operation)
{
    if (status != cudaSuccess)
        throw std::runtime_error(std::string(operation) + ": " + cudaGetErrorString(status));
}

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

double max_abs_error = 0.0;
double max_rel_error = 0.0;
std::map<std::string, std::pair<double, double>> field_errors;

void close(double device, double host, double tolerance, const char* field)
{
    if (std::isnan(host)) {
        require(std::isnan(device), field);
        return;
    }
    if (std::isinf(host)) {
        require(device == host, field);
        return;
    }
    const double abs_error = std::abs(device - host);
    const double rel_error = abs_error / std::max(1.0, std::abs(host));
    max_abs_error = std::max(max_abs_error, abs_error);
    max_rel_error = std::max(max_rel_error, rel_error);
    auto &statistics = field_errors[field];
    statistics.first = std::max(statistics.first, abs_error);
    statistics.second = std::max(statistics.second, rel_error);
    if (rel_error > tolerance)
        throw std::runtime_error(std::string(field) + " host/device mismatch");
}

struct Probe
{
    double gamma;
    double cv;
    double pressure_rho_T;
    double eint;
    double temperature;
    double pressure_rho_e;
    double sound_speed;
    double dp_drho;
    double dp_de;
    double total_energy;
    double state_P;
    double state_E;
    double state_cv;
    double state_sound_speed;
    double state_dp_drho;
    double state_dp_dT;
    double state_pele;
    double state_xne;
    double state_eta;
};

template <class View>
ARCH_INLINE Probe probe_leaf(View eos, const double* Xi, double rho, double T)
{
    Probe out{};
    out.gamma = eos.get_gamma(Xi);
    out.cv = eos.get_cv(rho, T, Xi);
    out.pressure_rho_T = eos.get_pressure_from_rho_T(rho, T, Xi);
    out.eint = eos.get_eint_from_T(rho, T, Xi);
    out.temperature = eos.get_temperature(rho, out.eint, Xi);
    out.pressure_rho_e = eos.get_pressure_from_rho_e(rho, out.eint, Xi);
    const FluidVector conservative{rho, 0.0, 0.0, 0.0, rho * out.eint};
    out.sound_speed = eos.get_sound_speed(conservative, out.pressure_rho_e, Xi);
    out.dp_drho = eos.get_dp_drho_e(rho, out.eint, Xi);
    out.dp_de = eos.get_dp_de_rho(rho, out.eint, Xi);
    out.total_energy = eos.get_total_energy_primitive(
        rho, 0.25, -0.5, 0.75, out.pressure_rho_T, Xi);
    eos_state_t state{};
    state.rho = rho;
    state.T = T;
    state.Xi = Xi;
    eos.evaluate_state(state);
    out.state_P = state.P;
    out.state_E = state.E;
    out.state_cv = state.cv;
    out.state_sound_speed = state.sound_speed;
    out.state_dp_drho = state.dp_drho;
    out.state_dp_dT = state.dp_dT;
    out.state_pele = state.pele;
    out.state_xne = state.xne;
    out.state_eta = state.eta;
    return out;
}

template <class View>
__global__ void probe_kernel(View eos, const double* Xi, double rho, double T, Probe* out)
{
    *out = probe_leaf(eos, Xi, rho, T);
}

template <class View>
Probe run_device(View view, const std::vector<double>& Xi, double rho, double T,
                 cudaStream_t stream)
{
    double* device_Xi = nullptr;
    Probe* device_probe = nullptr;
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_Xi), Xi.size() * sizeof(double)),
               "cudaMalloc Xi");
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_probe), sizeof(Probe)),
               "cudaMalloc probe");
    cuda_check(cudaMemcpyAsync(device_Xi, Xi.data(), Xi.size() * sizeof(double),
                               cudaMemcpyHostToDevice, stream), "copy Xi");
    probe_kernel<<<1, 1, 0, stream>>>(view, device_Xi, rho, T, device_probe);
    cuda_check(cudaGetLastError(), "probe launch");
    Probe result{};
    cuda_check(cudaMemcpyAsync(&result, device_probe, sizeof(result),
                               cudaMemcpyDeviceToHost, stream), "copy probe");
    std::vector<double> composition_after(Xi.size());
    cuda_check(cudaMemcpyAsync(composition_after.data(), device_Xi,
                               Xi.size() * sizeof(double), cudaMemcpyDeviceToHost, stream),
               "copy composition after probe");
    cuda_check(cudaStreamSynchronize(stream), "probe synchronize");
    require(composition_after == Xi, "EOS device leaf modified fixed composition");
    cuda_check(cudaFree(device_probe), "free probe");
    cuda_check(cudaFree(device_Xi), "free Xi");
    return result;
}

void compare(const Probe& device, const Probe& host, double direct_tol,
             double finite_difference_tol)
{
    close(device.gamma, host.gamma, direct_tol, "gamma");
    close(device.cv, host.cv, direct_tol, "cv");
    close(device.pressure_rho_T, host.pressure_rho_T, direct_tol, "pressure_rho_T");
    close(device.eint, host.eint, direct_tol, "eint");
    close(device.temperature, host.temperature, finite_difference_tol, "temperature");
    close(device.pressure_rho_e, host.pressure_rho_e, finite_difference_tol, "pressure_rho_e");
    close(device.sound_speed, host.sound_speed, finite_difference_tol, "sound_speed");
    close(device.dp_drho, host.dp_drho, finite_difference_tol, "dp_drho");
    close(device.dp_de, host.dp_de, finite_difference_tol, "dp_de");
    close(device.total_energy, host.total_energy, finite_difference_tol, "total_energy");
    close(device.state_P, host.state_P, direct_tol, "state.P");
    close(device.state_E, host.state_E, direct_tol, "state.E");
    close(device.state_cv, host.state_cv, direct_tol, "state.cv");
    close(device.state_sound_speed, host.state_sound_speed, finite_difference_tol,
          "state.sound_speed");
    close(device.state_dp_drho, host.state_dp_drho, finite_difference_tol,
          "state.dp_drho");
    close(device.state_dp_dT, host.state_dp_dT, finite_difference_tol, "state.dp_dT");
    close(device.state_pele, host.state_pele, direct_tol, "state.pele");
    close(device.state_xne, host.state_xne, direct_tol, "state.xne");
    close(device.state_eta, host.state_eta, direct_tol, "state.eta");
}

template <class View>
__global__ void scalar_kernel(View view, const double* Xi, int operation, double* result)
{
    if (operation == 0) *result = view.get_pressure_from_rho_T(0.0, 1.0e8, Xi);
    if (operation == 1) *result = view.get_pressure_from_rho_T(1.0e3, 1.0e8, Xi);
    if (operation == 2) *result = view.get_pressure_from_rho_T(1.0, 1.0e7, Xi);
    if (operation == 3) *result = view.get_pressure_from_rho_T(pow(10.0, 2.0 - 1.0e-6), 1.0e8, Xi);
}

template <class View>
double run_scalar(View view, const std::vector<double>& Xi, int operation,
                  cudaStream_t stream)
{
    double* device_Xi = nullptr;
    double* device_result = nullptr;
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_Xi), Xi.size() * sizeof(double)),
               "cudaMalloc scalar Xi");
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_result), sizeof(double)),
               "cudaMalloc scalar result");
    cuda_check(cudaMemcpyAsync(device_Xi, Xi.data(), Xi.size() * sizeof(double),
                               cudaMemcpyHostToDevice, stream), "copy scalar Xi");
    scalar_kernel<<<1, 1, 0, stream>>>(view, device_Xi, operation, device_result);
    cuda_check(cudaGetLastError(), "scalar launch");
    double result = 0.0;
    cuda_check(cudaMemcpyAsync(&result, device_result, sizeof(result),
                               cudaMemcpyDeviceToHost, stream), "copy scalar result");
    cuda_check(cudaStreamSynchronize(stream), "scalar synchronize");
    cuda_check(cudaFree(device_result), "free scalar result");
    cuda_check(cudaFree(device_Xi), "free scalar Xi");
    return result;
}

void assert_device_pointer(const void* pointer, const char* field)
{
    require(pointer != nullptr, field);
    cudaPointerAttributes attributes{};
    cuda_check(cudaPointerGetAttributes(&attributes, pointer), field);
    require(attributes.type == cudaMemoryTypeDevice, field);
}

std::vector<double> table3(double base, double di, double dj, double dk)
{
    std::vector<double> data(27);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                data[(i * 3 + j) * 3 + k] = base + i * di + j * dj + k * dk;
    data.back() += 0.125;
    return data;
}

std::vector<double> energy3()
{
    std::vector<double> data(27);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                data[(i * 3 + j) * 3 + k] = 1.0e6 * pow(10.0, 7.0 + j)
                    + 1000.0 * i + 100.0 * k;
    data.back() += 0.25;
    return data;
}

std::vector<double> table4(double base, double di, double dj, double dk, double dl)
{
    std::vector<double> data(81);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                for (int l = 0; l < 3; ++l)
                    data[((i * 3 + j) * 3 + k) * 3 + l] =
                        base + i * di + j * dj + k * dk + l * dl;
    data.back() += 0.375;
    return data;
}

std::vector<double> energy4()
{
    std::vector<double> data(81);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                for (int l = 0; l < 3; ++l)
                    data[((i * 3 + j) * 3 + k) * 3 + l] =
                        1.0e6 * pow(10.0, 7.0 + j) + 1000.0 * i + 100.0 * k + 10.0 * l;
    data.back() += 0.5;
    return data;
}

Tabular3DEOSHostView make_tab3(
    const SpeciesManager& species, const std::array<std::vector<double>, 6>& tables,
    bool dp_drho, bool dp_dT)
{
    Tabular3DEOSHostView view{};
    view.n_rho = view.n_T = view.n_X = 3;
    view.log_rho_min = 0.0; view.log_rho_max = 2.0; view.dlog_rho = 1.0;
    view.log_T_min = 7.0; view.log_T_max = 9.0; view.dlog_T = 1.0;
    view.X_min = 0.0; view.X_max = 1.0; view.dX = 0.5;
    view.table_P = tables[0].data();
    view.table_E = tables[1].data();
    view.table_cs = tables[2].data();
    view.table_cv = tables[3].data();
    view.table_dP_drho = dp_drho ? tables[4].data() : nullptr;
    view.table_dP_dT = dp_dT ? tables[5].data() : nullptr;
    view.specs = species.get_host_view();
    view.target_species_id = 1;
    return view;
}

Tabular4DEOSHostView make_tab4(
    const SpeciesManager& species, const std::array<std::vector<double>, 6>& tables,
    bool dp_drho, bool dp_dT)
{
    Tabular4DEOSHostView view{};
    view.n_rho = view.n_T = view.n_A = view.n_Z = 3;
    view.log_rho_min = 0.0; view.log_rho_max = 2.0; view.dlog_rho = 1.0;
    view.log_T_min = 7.0; view.log_T_max = 9.0; view.dlog_T = 1.0;
    view.A_min = 1.0; view.A_max = 2.0; view.dA = 0.5;
    view.Z_min = 0.5; view.Z_max = 1.0; view.dZ = 0.25;
    view.table_P = tables[0].data();
    view.table_E = tables[1].data();
    view.table_cs = tables[2].data();
    view.table_cv = tables[3].data();
    view.table_dP_drho = dp_drho ? tables[4].data() : nullptr;
    view.table_dP_dT = dp_dT ? tables[5].data() : nullptr;
    view.specs = species.get_host_view();
    return view;
}

struct SixValues { double value[6]; };

__global__ void tab3_final_kernel(Tabular3DEOSView view, SixValues* out)
{
    const int last = view.n_rho * view.n_T * view.n_X - 1;
    out->value[0] = view.table_P[last]; out->value[1] = view.table_E[last];
    out->value[2] = view.table_cs[last]; out->value[3] = view.table_cv[last];
    out->value[4] = view.table_dP_drho[last]; out->value[5] = view.table_dP_dT[last];
}

__global__ void tab4_final_kernel(Tabular4DEOSView view, SixValues* out)
{
    const int last = view.n_rho * view.n_T * view.n_A * view.n_Z - 1;
    out->value[0] = view.table_P[last]; out->value[1] = view.table_E[last];
    out->value[2] = view.table_cs[last]; out->value[3] = view.table_cv[last];
    out->value[4] = view.table_dP_drho[last]; out->value[5] = view.table_dP_dT[last];
}

struct HelmFinalValues { double f[9]; double ef[4]; };

__global__ void helm_final_kernel(HelmEosView view, HelmFinalValues* out)
{
    const int last = HelmEosView::imax * HelmEosView::jmax - 1;
    for (int i = 0; i < 9; ++i) out->f[i] = view.f[i][last];
    for (int i = 0; i < 4; ++i) out->ef[i] = view.ef_table[i][last];
}

__global__ void delayed_gamma_kernel(IdealGasView view, const double* Xi, double* result)
{
    const unsigned long long start = clock64();
    while (clock64() - start < 5000000ULL) {}
    *result = view.get_gamma(Xi);
}

void test_type_contracts()
{
    static_assert(std::is_trivially_copyable_v<SpeciesPODView>);
    static_assert(std::is_trivially_copyable_v<IdealGasView>);
    static_assert(std::is_trivially_copyable_v<HelmEosView>);
    static_assert(std::is_trivially_copyable_v<Tabular3DEOSView>);
    static_assert(std::is_trivially_copyable_v<Tabular4DEOSView>);
    static_assert(!std::is_copy_constructible_v<arch::cuda::DeviceSpeciesOwner>);
    static_assert(!std::is_copy_constructible_v<arch::cuda::HelmEosDeviceOwner>);
    static_assert(!std::is_copy_constructible_v<arch::cuda::Tabular3DEOSDeviceOwner>);
    static_assert(!std::is_copy_constructible_v<arch::cuda::Tabular4DEOSDeviceOwner>);
    static_assert(std::is_nothrow_move_constructible_v<arch::cuda::DeviceSpeciesOwner>);
    static_assert(std::is_nothrow_move_constructible_v<arch::cuda::HelmEosDeviceOwner>);
    static_assert(std::is_nothrow_move_constructible_v<arch::cuda::Tabular3DEOSDeviceOwner>);
    static_assert(std::is_nothrow_move_constructible_v<arch::cuda::Tabular4DEOSDeviceOwner>);
}

void test_ideal_and_lifetime(cudaStream_t stream)
{
    SpeciesManager species;
    species.add_species("a", 1.0, 1.0, 1.4, 700.0);
    species.add_species("b", 4.0, 2.0, 1.6, 900.0);
    const std::vector<double> Xi{0.25, 0.75};
    IdealGas host(1.55, species);
    const auto host_view = host.get_view();

    arch::cuda::DeviceSpeciesOwner first(species, stream);
    assert_device_pointer(first.view().A, "species A pointer");
    assert_device_pointer(first.view().Z, "species Z pointer");
    assert_device_pointer(first.view().gamma, "species gamma pointer");
    assert_device_pointer(first.view().Cv, "species Cv pointer");
    const Probe expected = probe_leaf(host_view, Xi.data(), 2.5, 4.0e6);
    compare(run_device(first.ideal_gas_view(1.55), Xi, 2.5, 4.0e6, stream),
            expected, 2.0e-13, 2.0e-13);

    const auto original_view = first.view();
    arch::cuda::DeviceSpeciesOwner moved(std::move(first));
    require(first.empty(), "moved-from species owner retained storage");
    require(moved.view().gamma == original_view.gamma,
            "species move changed allocation identity");
    compare(run_device(moved.ideal_gas_view(1.55), Xi, 2.5, 4.0e6, stream),
            expected, 2.0e-13, 2.0e-13);

    double* device_Xi = nullptr;
    double* device_result = nullptr;
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_Xi), 2 * sizeof(double)),
               "cudaMalloc lifetime Xi");
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_result), sizeof(double)),
               "cudaMalloc lifetime result");
    cuda_check(cudaMemcpyAsync(device_Xi, Xi.data(), 2 * sizeof(double),
                               cudaMemcpyHostToDevice, stream), "copy lifetime Xi");
    {
        arch::cuda::DeviceSpeciesOwner async_owner(species, stream);
        delayed_gamma_kernel<<<1, 1, 0, stream>>>(
            async_owner.ideal_gas_view(1.55), device_Xi, device_result);
        cuda_check(cudaGetLastError(), "delayed gamma launch");
        species.species_list[0].gamma_ref = 9.0;
        species.species_list[1].Cv_ref = 1.0;
    }
    double result = 0.0;
    cuda_check(cudaMemcpy(&result, device_result, sizeof(double), cudaMemcpyDeviceToHost),
               "copy lifetime result");
    close(result, host_view.get_gamma(Xi.data()), 2.0e-13,
          "owner destruction did not synchronize pending kernel");
    cuda_check(cudaFree(device_result), "free lifetime result");
    cuda_check(cudaFree(device_Xi), "free lifetime Xi");

    for (int i = 0; i < 3; ++i) {
        SpeciesManager repeated_species;
        repeated_species.add_species("a", 1.0, 1.0, 1.4, 700.0);
        repeated_species.add_species("b", 4.0, 2.0, 1.6, 900.0);
        arch::cuda::DeviceSpeciesOwner repeated(repeated_species, stream);
        compare(run_device(repeated.ideal_gas_view(1.55), Xi, 2.5, 4.0e6, stream),
                expected, 2.0e-13, 2.0e-13);
    }
}

void test_helm(cudaStream_t stream)
{
    SpeciesManager species;
    species.add_species("a", 1.0, 1.0, 1.4, 700.0);
    species.add_species("b", 4.0, 2.0, 1.6, 900.0);
    const std::vector<double> Xi{0.25, 0.75};
    HelmEos host(std::string(ARCH_SOURCE_DIR) +
                     "/EOS_toolkit/tables/helmholtz/helm_table.dat", &species);
    const auto host_view = host.get_view();
    arch::cuda::HelmEosDeviceOwner owner(host, stream);
    for (int i = 0; i < 9; ++i) assert_device_pointer(owner.view().f[i], "Helm f pointer");
    for (int i = 0; i < 4; ++i) assert_device_pointer(owner.view().ef_table[i], "Helm ef pointer");
    compare(run_device(owner.view(), Xi, 1.0e6, 1.0e8, stream),
            probe_leaf(host_view, Xi.data(), 1.0e6, 1.0e8), 2.0e-11, 5.0e-6);

    HelmFinalValues* device_values = nullptr;
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_values), sizeof(HelmFinalValues)),
               "cudaMalloc Helm finals");
    helm_final_kernel<<<1, 1, 0, stream>>>(owner.view(), device_values);
    cuda_check(cudaGetLastError(), "Helm final launch");
    HelmFinalValues values{};
    cuda_check(cudaMemcpyAsync(&values, device_values, sizeof(values),
                               cudaMemcpyDeviceToHost, stream), "copy Helm finals");
    cuda_check(cudaStreamSynchronize(stream), "Helm final synchronize");
    const int last = HelmEosHostView::imax * HelmEosHostView::jmax - 1;
    for (int i = 0; i < 9; ++i) {
        require(host_view.f[i][last] != 0.0, "Helm f final witness is zero");
        close(values.f[i], host_view.f[i][last], 0.0, "Helm f final element");
    }
    for (int i = 0; i < 4; ++i) {
        require(host_view.ef_table[i][last] != 0.0, "Helm ef final witness is zero");
        close(values.ef[i], host_view.ef_table[i][last], 0.0, "Helm ef final element");
    }
    cuda_check(cudaFree(device_values), "free Helm finals");

    auto moved = std::move(owner);
    require(owner.empty(), "moved-from Helm owner retained storage");
    compare(run_device(moved.view(), Xi, 1.0e6, 1.0e8, stream),
            probe_leaf(host_view, Xi.data(), 1.0e6, 1.0e8), 2.0e-11, 5.0e-6);
}

void check_six_finals(const SixValues& device,
                      const std::array<std::vector<double>, 6>& host)
{
    for (int i = 0; i < 6; ++i)
        close(device.value[i], host[i].back(), 0.0, "tabular final element");
}

void test_tabular3(cudaStream_t stream)
{
    SpeciesManager species;
    species.add_species("a", 1.0, 1.0, 1.4, 700.0);
    species.add_species("b", 4.0, 2.0, 1.6, 900.0);
    const std::vector<double> Xi{0.25, 0.75};
    const std::array<std::vector<double>, 6> tables{
        table3(1.0e14,1.0e12,2.0e12,3.0e12), energy3(),
        table3(2.0e7,1.0e5,2.0e5,3.0e5), table3(1.0e6,0.0,0.0,0.0),
        table3(7.0e6,1.0e4,2.0e4,3.0e4), table3(8.0e6,1.0e4,2.0e4,3.0e4)};
    const auto host = make_tab3(species, tables, true, true);
    arch::cuda::Tabular3DEOSDeviceOwner owner(host, stream);
    const auto view = owner.view();
    assert_device_pointer(view.table_P, "Tab3 P pointer");
    assert_device_pointer(view.table_E, "Tab3 E pointer");
    assert_device_pointer(view.table_cs, "Tab3 cs pointer");
    assert_device_pointer(view.table_cv, "Tab3 cv pointer");
    assert_device_pointer(view.table_dP_drho, "Tab3 dp_drho pointer");
    assert_device_pointer(view.table_dP_dT, "Tab3 dp_dT pointer");
    compare(run_device(view, Xi, 10.0, 1.0e8, stream),
            probe_leaf(host, Xi.data(), 10.0, 1.0e8), 2.0e-13, 2.0e-10);
    close(run_scalar(view, Xi, 0, stream), host.get_pressure_from_rho_T(0.0,1.0e8,Xi.data()),
          0.0, "Tab3 invalid branch");
    close(run_scalar(view, Xi, 1, stream), host.get_pressure_from_rho_T(1.0e3,1.0e8,Xi.data()),
          2.0e-13, "Tab3 fallback branch");
    close(run_scalar(view, Xi, 2, stream), host.get_pressure_from_rho_T(1.0,1.0e7,Xi.data()),
          2.0e-13, "Tab3 lower bound");
    close(run_scalar(view, Xi, 3, stream),
          host.get_pressure_from_rho_T(pow(10.0,2.0-1.0e-6),1.0e8,Xi.data()),
          2.0e-13, "Tab3 upper margin");

    SixValues* device_values = nullptr;
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_values), sizeof(SixValues)),
               "cudaMalloc Tab3 finals");
    tab3_final_kernel<<<1,1,0,stream>>>(view, device_values);
    SixValues values{};
    cuda_check(cudaMemcpyAsync(&values, device_values, sizeof(values),
                               cudaMemcpyDeviceToHost, stream), "copy Tab3 finals");
    cuda_check(cudaStreamSynchronize(stream), "Tab3 final synchronize");
    check_six_finals(values, tables);
    cuda_check(cudaFree(device_values), "free Tab3 finals");

    const auto host_null = make_tab3(species, tables, false, false);
    arch::cuda::Tabular3DEOSDeviceOwner null_owner(host_null, stream);
    require(null_owner.view().table_dP_drho == nullptr, "Tab3 optional dp_drho not null");
    require(null_owner.view().table_dP_dT == nullptr, "Tab3 optional dp_dT not null");
    const Probe null_expected = probe_leaf(host_null, Xi.data(), 10.0, 1.0e8);
    close(null_expected.state_dp_dT, 8685.8896664062504, 0.0,
          "Tab3 latest-main dp_dT finite-difference oracle");
    compare(run_device(null_owner.view(), Xi, 10.0, 1.0e8, stream),
            null_expected, 2.0e-13, 2.0e-10);

    auto moved = std::move(owner);
    require(owner.empty(), "moved-from Tab3 owner retained storage");
    compare(run_device(moved.view(), Xi, 10.0, 1.0e8, stream),
            probe_leaf(host, Xi.data(), 10.0, 1.0e8), 2.0e-13, 2.0e-10);
}

void test_tabular4(cudaStream_t stream)
{
    SpeciesManager species;
    species.add_species("q", 1.5, 0.75, 1.4, 1.0e6);
    const std::vector<double> Xi{1.0};
    const std::array<std::vector<double>, 6> tables{
        table4(2.0e14,1.0e12,2.0e12,3.0e12,4.0e12), energy4(),
        table4(3.0e7,1.0e5,2.0e5,3.0e5,4.0e5), table4(1.0e6,0.0,0.0,0.0,0.0),
        table4(9.0e6,1.0e4,2.0e4,3.0e4,4.0e4),
        table4(1.0e7,1.0e4,2.0e4,3.0e4,4.0e4)};
    const auto host = make_tab4(species, tables, true, true);
    arch::cuda::Tabular4DEOSDeviceOwner owner(host, stream);
    const auto view = owner.view();
    assert_device_pointer(view.table_P, "Tab4 P pointer");
    assert_device_pointer(view.table_E, "Tab4 E pointer");
    assert_device_pointer(view.table_cs, "Tab4 cs pointer");
    assert_device_pointer(view.table_cv, "Tab4 cv pointer");
    assert_device_pointer(view.table_dP_drho, "Tab4 dp_drho pointer");
    assert_device_pointer(view.table_dP_dT, "Tab4 dp_dT pointer");
    compare(run_device(view, Xi, 10.0, 1.0e8, stream),
            probe_leaf(host, Xi.data(), 10.0, 1.0e8), 2.0e-13, 2.0e-10);
    close(run_scalar(view, Xi, 0, stream), host.get_pressure_from_rho_T(0.0,1.0e8,Xi.data()),
          0.0, "Tab4 invalid branch");
    close(run_scalar(view, Xi, 1, stream), host.get_pressure_from_rho_T(1.0e3,1.0e8,Xi.data()),
          2.0e-13, "Tab4 fallback branch");
    close(run_scalar(view, Xi, 2, stream), host.get_pressure_from_rho_T(1.0,1.0e7,Xi.data()),
          2.0e-13, "Tab4 lower bound");
    close(run_scalar(view, Xi, 3, stream),
          host.get_pressure_from_rho_T(pow(10.0,2.0-1.0e-6),1.0e8,Xi.data()),
          2.0e-13, "Tab4 upper margin");

    SixValues* device_values = nullptr;
    cuda_check(cudaMalloc(reinterpret_cast<void**>(&device_values), sizeof(SixValues)),
               "cudaMalloc Tab4 finals");
    tab4_final_kernel<<<1,1,0,stream>>>(view, device_values);
    SixValues values{};
    cuda_check(cudaMemcpyAsync(&values, device_values, sizeof(values),
                               cudaMemcpyDeviceToHost, stream), "copy Tab4 finals");
    cuda_check(cudaStreamSynchronize(stream), "Tab4 final synchronize");
    check_six_finals(values, tables);
    cuda_check(cudaFree(device_values), "free Tab4 finals");

    const auto host_null = make_tab4(species, tables, false, false);
    arch::cuda::Tabular4DEOSDeviceOwner null_owner(host_null, stream);
    require(null_owner.view().table_dP_drho == nullptr, "Tab4 optional dp_drho not null");
    require(null_owner.view().table_dP_dT == nullptr, "Tab4 optional dp_dT not null");
    compare(run_device(null_owner.view(), Xi, 10.0, 1.0e8, stream),
            probe_leaf(host_null, Xi.data(), 10.0, 1.0e8), 2.0e-13, 2.0e-10);

    auto moved = std::move(owner);
    require(owner.empty(), "moved-from Tab4 owner retained storage");
    compare(run_device(moved.view(), Xi, 10.0, 1.0e8, stream),
            probe_leaf(host, Xi.data(), 10.0, 1.0e8), 2.0e-13, 2.0e-10);
}

} // namespace

int main()
{
    try {
        test_type_contracts();
        cudaStream_t stream = nullptr;
        cuda_check(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking), "create stream");
        test_ideal_and_lifetime(stream);
        test_helm(stream);
        test_tabular3(stream);
        test_tabular4(stream);
        cuda_check(cudaStreamDestroy(stream), "destroy stream");
        std::cout << "eos_host_device_parity max_abs=" << max_abs_error
                  << " max_rel=" << max_rel_error << "\n";
        for (const auto &[field, statistics] : field_errors)
            std::cout << "field=" << field << " max_abs=" << statistics.first
                      << " max_rel=" << statistics.second << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "eos_host_device_parity: " << error.what() << "\n";
        return 1;
    }
}
