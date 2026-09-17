"""Prepare an annotation-only Jacobian sink variant and a timed existing test.

The generated rate/network mathematical bodies are untouched. Outputs are new
isolated copies; input SHA and reversible single-token change are mandatory.
"""
import hashlib
from pathlib import Path

DRIVER_SHA = '7358f0970540589bd6b3b63d8e495be770146b950ea3203d6d3548a6f8948950'
MATH_SHA = {150: '0b2978ad32a148b0c7908e36ec58885eaa29ab30f45309630d88ae42e8c0f5d5',
            200: '65d97aa14ba2448c2d10d8ac5b5409552565824772a4eca98349a35f2c63ab94'}


def sink_variant(original, network):
    if hashlib.sha256(original).hexdigest() != MATH_SHA[network]:
        raise ValueError('unreviewed generated adapter')
    before = b'    ARCH_HEAVY_INLINE void set(int row, int column, double value) {'
    after = b'    ARCH_INLINE void set(int row, int column, double value) {'
    if original.count(before) != 1:
        raise ValueError('ambiguous small sink annotation')
    changed = original.replace(before, after, 1)
    if changed.replace(after, before, 1) != original:
        raise ValueError('change outside annotation')
    return changed


EXTRA_INCLUDES = '''#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
'''
COMPOSITION = '''        const char* composition = std::getenv("ARCH_LEAF_COMPOSITION");
        if (composition == nullptr || (std::strcmp(composition, "uniform") != 0
                                      && std::strcmp(composition, "co") != 0))
            throw std::invalid_argument("explicit uniform/co diagnostic composition required");
        if (std::strcmp(composition, "co") == 0) {
            int carbon = -1, oxygen = -1;
            for (int i = 0; i < Network::NUM_SPECIES; ++i) {
                state[i] = 1.e-30;
                if (std::strcmp(Network::SPECIES_NAMES[i], "c12") == 0) carbon = i;
                if (std::strcmp(Network::SPECIES_NAMES[i], "o16") == 0) oxygen = i;
            }
            if (carbon < 0 || oxygen < 0) throw std::runtime_error("missing C12/O16");
            state[carbon] = 0.5; state[oxygen] = 0.5;
        }
        std::cout << "LEAF_INPUT composition=" << composition << " neq=" << Network::ODE_NEQ
                  << " rho=" << rho << " temperature=" << initial_temperature << '\\n';
'''
TIMING = '''        cudaFuncAttributes attributes{};
        check(cudaFuncGetAttributes(&attributes, evaluate));
        std::cout << "LEAF_STATIC_RESOURCES registers=" << attributes.numRegs
                  << " local_bytes=" << attributes.localSizeBytes
                  << " shared_bytes=" << attributes.sharedSizeBytes << '\\n';
        const auto launch = [&]() {
            evaluate<<<1, 1>>>(device_state.data, device_matrix, device_rhs.data,
                device_energy.data, device_temperature.data, scalars.data, device_network, rho);
            check(cudaGetLastError());
        };
        for (int warm = 0; warm < 5; ++warm) launch();
        check(cudaDeviceSynchronize());
        struct Event {
            cudaEvent_t value{};
            Event() { check(cudaEventCreate(&value)); }
            ~Event() { cudaEventDestroy(value); }
        };
        Event begin_event, end_event;
        constexpr int repeats = 20;
        for (int sample = 0; sample < 5; ++sample) {
            const auto start = std::chrono::steady_clock::now();
            for (int repeat = 0; repeat < repeats; ++repeat) {
                matrix.zero();
                host_network.eval_rhs(state.data(), rho, 0.0, rhs.data(), energy_value);
                host_network.eval_jacobian(state.data(), rho, 0.0, matrix, energy.data());
                host_network.eval_temperature_derivative(state.data(), rho, 0.0, temperature.data(), temperature_value);
            }
            const double cpu_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count() / repeats;
            check(cudaEventRecord(begin_event.value));
            for (int repeat = 0; repeat < repeats; ++repeat) launch();
            check(cudaEventRecord(end_event.value));
            check(cudaEventSynchronize(end_event.value));
            float elapsed = 0.0f;
            check(cudaEventElapsedTime(&elapsed, begin_event.value, end_event.value));
            std::cout << std::setprecision(17) << "LEAF_EVENT_TIMING sample=" << sample
                      << " repeats=" << repeats << " cpu_ms=" << cpu_ms
                      << " gpu_ms=" << static_cast<double>(elapsed) / repeats << '\\n';
        }
'''
SNAPSHOT = '''        const char* snapshot_path = std::getenv("ARCH_LEAF_SNAPSHOT");
        if (!snapshot_path || std::filesystem::exists(snapshot_path))
            throw std::invalid_argument("new snapshot path required");
        std::ofstream snapshot(snapshot_path, std::ios::binary);
        snapshot.exceptions(std::ios::badbit | std::ios::failbit);
        const std::uint64_t header[]{0x415243484c454146ULL, Network::ODE_NEQ, nnz};
        snapshot.write(reinterpret_cast<const char*>(header), sizeof(header));
        const auto dump = [&](const std::vector<double>& cpu, const std::vector<double>& gpu) {
            const std::uint64_t count = cpu.size();
            if (gpu.size() != count) throw std::runtime_error("snapshot shape mismatch");
            snapshot.write(reinterpret_cast<const char*>(&count), sizeof(count));
            snapshot.write(reinterpret_cast<const char*>(cpu.data()), count * sizeof(double));
            snapshot.write(reinterpret_cast<const char*>(gpu.data()), count * sizeof(double));
        };
        dump(values, download(device_values.data, nnz));
        dump(rhs, download(device_rhs.data, rhs.size()));
        dump(energy, download(device_energy.data, energy.size()));
        dump(temperature, download(device_temperature.data, temperature.size()));
        dump({energy_value, temperature_value, 1.0}, download(scalars.data, 3));
        snapshot.close();
        std::cout << "LEAF_SNAPSHOT_WRITTEN endian="
                  << (*reinterpret_cast<const unsigned char*>(header) == 0x46 ? "little" : "other") << '\\n';
'''


def timed_driver(original):
    if hashlib.sha256(original).hexdigest() != DRIVER_SHA:
        raise ValueError('unreviewed original leaf parity driver')
    text = original.decode('utf-8')
    newline = '\r\n' if text.count('\r\n') == text.count('\n') else '\n'
    changes = [('#include <memory>' + newline, '#include <memory>' + newline + EXTRA_INCLUDES.replace('\n', newline)),
               ('        arch::linalg::CsrPatternBuilder builder(Network::ODE_NEQ);',
                COMPOSITION.replace('\n', newline) + '        arch::linalg::CsrPatternBuilder builder(Network::ODE_NEQ);'),
               ('        const auto download = [&](const double* pointer, std::size_t count) {',
                TIMING.replace('\n', newline) + '        const auto download = [&](const double* pointer, std::size_t count) {'),
               ('        std::cout << "GENERATED_NETWORK_MATH_PASS neq="',
                SNAPSHOT.replace('\n', newline) + '        std::cout << "GENERATED_NETWORK_MATH_PASS neq="')]
    for before, after in changes:
        if text.count(before) != 1:
            raise ValueError('ambiguous test instrumentation insertion')
        text = text.replace(before, after, 1)
    restored = text
    for before, after in reversed(changes):
        restored = restored.replace(after, before, 1)
    if restored.encode('utf-8') != original:
        raise ValueError('change outside test instrumentation')
    return text.encode('utf-8')
