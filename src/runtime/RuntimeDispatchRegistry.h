#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

namespace arch::runtime {

enum class EosKind
{
    Ideal,
    Tabular3,
    Tabular4,
    Helmholtz
};

enum class NetworkKind
{
    Disabled,
    Iso7,
    Aprox13,
    Aprox19,
    Aprox21
};

enum class OdeKind
{
    Disabled,
    BeNr,
    Ros4,
    Bd
};

enum class LinearSolverKind
{
    Disabled,
    DenseLu,
    SparseKlu
};

enum class FluxKind
{
    VanLeer,
    StegerWarming,
    Roe,
    Hll,
    Hllc
};

enum class ReconstructionKind
{
    Pcm,
    Ppm,
    MusclMinmod,
    MusclSuperbee,
    MusclVanLeer,
    MusclMc
};

enum class IntegratorKind
{
    Euler,
    Rk2,
    Rk3
};

/**
 * @brief Fully resolved runtime selection used to locate one compiled launcher.
 */
struct DispatchKey
{
    EosKind eos{EosKind::Ideal};
    NetworkKind network{NetworkKind::Disabled};
    OdeKind ode{OdeKind::Disabled};
    LinearSolverKind linear_solver{LinearSolverKind::Disabled};
    FluxKind flux{FluxKind::Hllc};
    ReconstructionKind reconstruction{ReconstructionKind::Pcm};
    IntegratorKind integrator{IntegratorKind::Rk2};

    bool operator==(const DispatchKey &) const noexcept = default;
};

/**
 * @brief Stable, non-template host ABI passed to a registered launch wrapper.
 *
 * Concrete wrappers cast these opaque pointers to their known host types, then
 * invoke an explicitly instantiated CPU function or launch a CUDA kernel. This
 * structure and the registry are host-only; they are never copied to device
 * code and contain no virtual functions or std::function objects.
 */
struct DispatchInvocation
{
    void *simulation_state{nullptr};
    const void *simulation_config{nullptr};
    const void *runtime_services{nullptr};
};

using DispatchLauncher = void (*)(DispatchInvocation &);

struct DispatchEntry
{
    DispatchLauncher launcher{nullptr};
    std::string label;
};

std::string describe_dispatch_key(const DispatchKey &key);

/**
 * @brief Host-side capability registry for compiled dispatch launchers.
 *
 * Registration is expected during single-threaded startup. After registration
 * completes, concurrent const lookups are safe. The registry intentionally
 * contains no implicit CPU fallback: require() either returns the exact key or
 * throws with a description of the unsupported combination.
 */
class RuntimeDispatchRegistry
{
public:
    void register_launcher(const DispatchKey &key,
                           DispatchLauncher launcher,
                           std::string_view label);

    const DispatchEntry *find(const DispatchKey &key) const noexcept;
    const DispatchEntry &require(const DispatchKey &key) const;

    std::size_t size() const noexcept { return entries_.size(); }
    bool empty() const noexcept { return entries_.empty(); }

private:
    struct DispatchKeyHash
    {
        std::size_t operator()(const DispatchKey &key) const noexcept;
    };

    std::unordered_map<DispatchKey, DispatchEntry, DispatchKeyHash> entries_;
};

} // namespace arch::runtime
