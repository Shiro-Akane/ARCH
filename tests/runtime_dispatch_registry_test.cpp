#include "runtime/RuntimeDispatchRegistry.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

int launcher_calls = 0;

void host_test_launcher(arch::runtime::DispatchInvocation &invocation)
{
    if (invocation.simulation_state == nullptr
        || invocation.simulation_config == nullptr) {
        throw std::runtime_error("test launcher received an incomplete invocation");
    }
    ++launcher_calls;
}

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

} // namespace

int main()
{
    using namespace arch::runtime;

    bool passed = true;
    RuntimeDispatchRegistry registry;

    const DispatchKey cpu_key{
        ComputeBackend::Cpu,
        EosKind::Helmholtz,
        NetworkKind::Aprox19,
        OdeKind::BeNr,
        LinearSolverKind::DenseLu,
        FluxKind::Hllc,
        ReconstructionKind::MusclMc,
        IntegratorKind::Rk2};

    registry.register_launcher(cpu_key, &host_test_launcher, "host-test-only");
    passed &= expect(registry.size() == 1, "one CPU launcher should be registered");

    const DispatchEntry *found = registry.find(cpu_key);
    passed &= expect(found != nullptr, "exact CPU key should be found");
    passed &= expect(found != nullptr && found->label == "host-test-only",
                     "registered label should be preserved");

    int state = 1;
    const int config = 2;
    DispatchInvocation invocation{&state, &config, nullptr};
    registry.require(cpu_key).launcher(invocation);
    passed &= expect(launcher_calls == 1, "registered host launcher should run once");

    bool duplicate_failed = false;
    try {
        registry.register_launcher(cpu_key, &host_test_launcher, "duplicate");
    } catch (const std::logic_error &) {
        duplicate_failed = true;
    }
    passed &= expect(duplicate_failed, "duplicate registration must fail loudly");

    DispatchKey cuda_key = cpu_key;
    cuda_key.backend = ComputeBackend::Cuda;
    passed &= expect(registry.find(cuda_key) == nullptr,
                     "no CUDA production launcher should be implied");

    bool missing_cuda_failed = false;
    try {
        static_cast<void>(registry.require(cuda_key));
    } catch (const std::runtime_error &error) {
        const std::string message = error.what();
        missing_cuda_failed = message.find("backend=cuda") != std::string::npos
                           && message.find("no implicit CPU") != std::string::npos;
    }
    passed &= expect(missing_cuda_failed,
                     "missing CUDA combination must fail without CPU fallback");

    DispatchKey auto_key = cpu_key;
    auto_key.backend = ComputeBackend::Auto;
    bool unresolved_auto_failed = false;
    try {
        static_cast<void>(registry.require(auto_key));
    } catch (const std::invalid_argument &) {
        unresolved_auto_failed = true;
    }
    passed &= expect(unresolved_auto_failed,
                     "unresolved auto backend must be rejected by require()");

    bool null_launcher_failed = false;
    DispatchKey second_cpu_key = cpu_key;
    second_cpu_key.integrator = IntegratorKind::Rk3;
    try {
        registry.register_launcher(second_cpu_key, nullptr, "null-launcher");
    } catch (const std::invalid_argument &) {
        null_launcher_failed = true;
    }
    passed &= expect(null_launcher_failed, "null launcher registration must fail");

    if (!passed) {
        return 1;
    }

    std::cout << "runtime_dispatch_registry_test: PASS\n";
    return 0;
}
