#include "../math/BurnThermalCases.h"
#include <cuda_runtime.h>
#include <iomanip>
#include <iostream>

namespace fixture = arch::test::burn_thermal;
struct Result {
    bool jacobian;
    bool first_law;
    bool accepted_state;
    bool composition_energy;
    fixture::Convergence paths[2];
    fixture::ToleranceControl backward_euler;
    fixture::ExternalEnergySuite external_energy;
    fixture::BoundNetworkResult bound_network;
    fixture::Ros4FailureControl ros_failure;
    fixture::ThermalTranslationControl bd_translation, ros_translation;
    fixture::ThermalTranslationControl bd_substeps, ros_substeps;
    fixture::ThermalTranslationControl be_translation, be_substeps;
};
__global__ void evaluate(Result* result, BurnConfigView config)
{
    result->jacobian = fixture::jacobian_contract();
    result->first_law = fixture::first_law_contract();
    result->accepted_state = fixture::accepted_state_contract();
    result->composition_energy = fixture::composition_energy_control<Solver_BE_NR>(config)
        && fixture::composition_energy_control<Solver_BD>(config)
        && fixture::composition_energy_control<Solver_ROS4>(config);
    result->paths[0] = fixture::ros4_convergence(false, config);
    result->paths[1] = fixture::ros4_convergence(true, config);
    result->backward_euler = fixture::be_tolerance_control(config);
    result->external_energy = fixture::external_energy_suite(config);
    result->bound_network = fixture::bound_network_suite(config);
    result->ros_failure = fixture::ros4_failure_control(config);
    result->bd_translation = fixture::translation_control<Solver_BD>(config);
    result->ros_translation = fixture::translation_control<Solver_ROS4>(config);
    result->bd_substeps = fixture::translation_control<Solver_BD>(config, 16);
    result->ros_substeps = fixture::translation_control<Solver_ROS4>(config, 16);
    result->be_translation = fixture::translation_control<Solver_BE_NR>(config);
    result->be_substeps = fixture::translation_control<Solver_BE_NR>(config, 16);
}
int main()
{
    int count = 0;
    const auto probe = cudaGetDeviceCount(&count);
    if (probe == cudaErrorNoDevice || probe == cudaErrorInsufficientDriver
        || (probe == cudaSuccess && count == 0)) return 77;
    if (probe != cudaSuccess) return 1;
    Result* device = nullptr;
    if (cudaMalloc(&device, sizeof(Result)) != cudaSuccess) return 1;
    const auto config = fixture::convergence_config();
    evaluate<<<1, 1>>>(device, config);
    const auto launch = cudaGetLastError();
    Result result{};
    const auto copy = cudaMemcpy(&result, device, sizeof(Result), cudaMemcpyDeviceToHost);
    const auto release = cudaFree(device);
    bool passed = launch == cudaSuccess && copy == cudaSuccess && release == cudaSuccess
        && result.jacobian && fixture::jacobian_contract()
        && result.first_law && fixture::first_law_contract();
    passed = passed && result.accepted_state && fixture::accepted_state_contract();
    passed = passed && result.composition_energy
        && fixture::composition_energy_control<Solver_BE_NR>(config)
        && fixture::composition_energy_control<Solver_BD>(config)
        && fixture::composition_energy_control<Solver_ROS4>(config);
    const auto host_failure = fixture::ros4_failure_control(config);
    passed = passed && host_failure.success && result.ros_failure.success;
    for (int lane = 0; lane < 4; ++lane) {
        const auto& host = host_failure.reports[lane];
        const auto& gpu = result.ros_failure.reports[lane];
        passed = passed && gpu.dt_recommended == host.dt_recommended
            && gpu.status == host.status && gpu.attempted_substeps == host.attempted_substeps
            && gpu.rejected_substeps == host.rejected_substeps
            && gpu.energy_change == host.energy_change;
        std::cout << "ROS_FAILURE " << lane
                  << " host_status=" << static_cast<int>(host.status)
                  << " gpu_status=" << static_cast<int>(gpu.status)
                  << " host_attempts=" << host.attempted_substeps
                  << " gpu_attempts=" << gpu.attempted_substeps
                  << " host_rejected=" << host.rejected_substeps
                  << " gpu_rejected=" << gpu.rejected_substeps
                  << " host_pass=" << host_failure.lane_pass[lane]
                  << " gpu_pass=" << result.ros_failure.lane_pass[lane]
                  << " host_nonfinite=" << host_failure.nonfinite_matrix[lane]
                  << " gpu_nonfinite=" << result.ros_failure.nonfinite_matrix[lane]
                  << " gpu_dt=" << std::setprecision(17) << gpu.dt_recommended << '\n';
    }
    const auto host_be = fixture::be_tolerance_control(config);
    passed = passed && host_be.success && result.backward_euler.success;
    const auto host_bd = fixture::translation_control<Solver_BD>(config);
    const auto host_ros = fixture::translation_control<Solver_ROS4>(config);
    const auto host_bd_substeps = fixture::translation_control<Solver_BD>(config, 16);
    const auto host_ros_substeps = fixture::translation_control<Solver_ROS4>(config, 16);
    const auto host_be_translation = fixture::translation_control<Solver_BE_NR>(config);
    const auto host_be_substeps = fixture::translation_control<Solver_BE_NR>(config, 16);
    passed = passed && host_bd.success && result.bd_translation.success;
    passed = passed && host_ros.success && result.ros_translation.success;
    passed = passed && host_bd_substeps.success && result.bd_substeps.success
        && host_ros_substeps.success && result.ros_substeps.success;
    passed = passed && host_be_translation.success && result.be_translation.success
        && host_be_substeps.success && result.be_substeps.success;
    for (int i = 0; i < 4; ++i) {
        std::cout << "BE_TRANSLATION " << i << " host_ulps=" << host_be_translation.temperature_error_ulps[i]
                  << " device_ulps=" << result.be_translation.temperature_error_ulps[i] << '\n';
        std::cout << "BE_SUBSTEPS " << i << " host_ulps=" << host_be_substeps.temperature_error_ulps[i]
                  << " device_ulps=" << result.be_substeps.temperature_error_ulps[i] << '\n';
        std::cout << "BD_TRANSLATION " << i << " host_ulps=" << host_bd.temperature_error_ulps[i]
                  << " device_ulps=" << result.bd_translation.temperature_error_ulps[i] << '\n';
        std::cout << "ROS_TRANSLATION " << i << " host_ulps=" << host_ros.temperature_error_ulps[i]
                  << " device_ulps=" << result.ros_translation.temperature_error_ulps[i] << '\n';
        std::cout << "BD_SUBSTEPS " << i << " host_ulps=" << host_bd_substeps.temperature_error_ulps[i]
                  << " device_ulps=" << result.bd_substeps.temperature_error_ulps[i] << '\n';
        std::cout << "ROS_SUBSTEPS " << i << " host_ulps=" << host_ros_substeps.temperature_error_ulps[i]
                  << " device_ulps=" << result.ros_substeps.temperature_error_ulps[i] << '\n';
    }
    const auto external = fixture::external_energy_suite(config);
    const auto bound = fixture::bound_network_suite(config);
    passed = passed && bound.success && result.bound_network.success;
    for (int method = 0; method < 3; ++method)
        for (int lane = 0; lane < 2; ++lane)
            passed = passed && std::isfinite(result.bound_network.abundance[method][lane])
                && std::abs(bound.abundance[method][lane]
                    - result.bound_network.abundance[method][lane]) < 1.e-12;
    passed = passed && external.jacobian && result.external_energy.jacobian;
    constexpr const char* methods[]{"BE_NR", "BD", "ROS4"};
    for (int method = 0; method < 3; ++method)
        passed = passed && external.rollback[method] && result.external_energy.rollback[method];
    for (int method = 0; method < 3; ++method)
        for (int reacting = 0; reacting < 2; ++reacting) {
            const auto& host = external.paths[method][reacting];
            const auto& gpu = result.external_energy.paths[method][reacting];
            passed = passed && host.success && gpu.success;
            for (int level = 0; level < 3; ++level) {
                for (int component = 0; component < 4; ++component)
                    passed = passed && std::isfinite(gpu.accepted_state[level][component])
                        && std::abs(host.accepted_state[level][component]
                            - gpu.accepted_state[level][component]) < 1.e-12;
                std::cout << std::setprecision(17) << "EXTERNAL_ENERGY " << methods[method]
                    << " reacting=" << reacting << " steps=" << (8 << level)
                    << " host_t_error=" << host.temperature_error[level]
                    << " device_t_error=" << gpu.temperature_error[level]
                    << " host_integral_error=" << host.quadrature_error[level]
                    << " device_integral_error=" << gpu.quadrature_error[level] << '\n';
            }
        }
    for (int level = 0; level < 2; ++level)
        std::cout << std::setprecision(17) << "BE_CONTROL " << level << " host_error=" << host_be.error[level]
            << " device_error=" << result.backward_euler.error[level]
            << " host_attempts=" << host_be.attempts[level]
            << " device_attempts=" << result.backward_euler.attempts[level] << '\n';
    std::cout << std::setprecision(17) << "composition_cv,steps,cpu_x_error,gpu_x_error,cpu_t_error,gpu_t_error\n";
    for (int path = 0; path < 2; ++path) {
        const auto host = fixture::ros4_convergence(path != 0, config);
        const auto gpu = result.paths[path];
        passed = passed && host.success && gpu.success;
        for (int level = 0; level < 3; ++level) {
            std::cout << path << ',' << (16 << level) << ',' << host.composition_error[level]
                << ',' << gpu.composition_error[level] << ',' << host.temperature_error[level]
                << ',' << gpu.temperature_error[level] << '\n';
        }
    }
    std::cout << "CONTRACTS jacobian=" << result.jacobian << " first_law=" << result.first_law
              << " accepted_state=" << result.accepted_state << " ros_failure=" << result.ros_failure.success
              << " be_control=" << result.backward_euler.success
              << " bound_network=" << result.bound_network.success
              << " external_jacobian=" << result.external_energy.jacobian;
    for (int method = 0; method < 3; ++method)
        std::cout << " external_" << method << '=' << result.external_energy.rollback[method]
                  << ',' << result.external_energy.paths[method][0].success
                  << ',' << result.external_energy.paths[method][1].success;
    std::cout << '\n';
    return passed ? 0 : 1;
}
