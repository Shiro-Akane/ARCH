#include "RuntimeDispatchRegistry.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace arch::runtime {
namespace {

std::string_view eos_name(EosKind value) noexcept
{
    switch (value) {
    case EosKind::Ideal: return "ideal";
    case EosKind::Tabular3: return "tabular3";
    case EosKind::Tabular4: return "tabular4";
    case EosKind::Helmholtz: return "helmholtz";
    }
    return "invalid";
}

std::string_view network_name(NetworkKind value) noexcept
{
    switch (value) {
    case NetworkKind::Disabled: return "disabled";
    case NetworkKind::Iso7: return "iso7";
    case NetworkKind::Aprox13: return "aprox13";
    case NetworkKind::Aprox19: return "aprox19";
    case NetworkKind::Aprox21: return "aprox21";
    }
    return "invalid";
}

std::string_view ode_name(OdeKind value) noexcept
{
    switch (value) {
    case OdeKind::Disabled: return "disabled";
    case OdeKind::BeNr: return "be_nr";
    case OdeKind::Ros4: return "ros4";
    case OdeKind::Bd: return "bd";
    }
    return "invalid";
}

std::string_view linear_solver_name(LinearSolverKind value) noexcept
{
    switch (value) {
    case LinearSolverKind::Disabled: return "disabled";
    case LinearSolverKind::DenseLu: return "dense_lu";
    case LinearSolverKind::SparseKlu: return "sparse_klu";
    }
    return "invalid";
}

std::string_view flux_name(FluxKind value) noexcept
{
    switch (value) {
    case FluxKind::VanLeer: return "van_leer";
    case FluxKind::StegerWarming: return "steger_warming";
    case FluxKind::Roe: return "roe";
    case FluxKind::Hll: return "hll";
    case FluxKind::Hllc: return "hllc";
    }
    return "invalid";
}

std::string_view reconstruction_name(ReconstructionKind value) noexcept
{
    switch (value) {
    case ReconstructionKind::Pcm: return "pcm";
    case ReconstructionKind::Ppm: return "ppm";
    case ReconstructionKind::MusclMinmod: return "muscl_minmod";
    case ReconstructionKind::MusclSuperbee: return "muscl_superbee";
    case ReconstructionKind::MusclVanLeer: return "muscl_vanleer";
    case ReconstructionKind::MusclMc: return "muscl_mc";
    }
    return "invalid";
}

std::string_view integrator_name(IntegratorKind value) noexcept
{
    switch (value) {
    case IntegratorKind::Euler: return "euler";
    case IntegratorKind::Rk2: return "rk2";
    case IntegratorKind::Rk3: return "rk3";
    }
    return "invalid";
}

void validate_resolved_key(const DispatchKey &key, std::string_view operation)
{
    if (key.backend == ComputeBackend::Auto) {
        throw std::invalid_argument(
            std::string(operation)
            + " requires a resolved backend; resolve 'auto' to 'cpu' or 'cuda' first");
    }
}

std::size_t mix_hash(std::size_t seed, std::size_t value) noexcept
{
    constexpr std::size_t golden_ratio = static_cast<std::size_t>(0x9e3779b9U);
    return seed ^ (value + golden_ratio + (seed << 6U) + (seed >> 2U));
}

} // namespace

std::string describe_dispatch_key(const DispatchKey &key)
{
    std::ostringstream out;
    out << "backend=" << compute_backend_name(key.backend)
        << ",eos=" << eos_name(key.eos)
        << ",network=" << network_name(key.network)
        << ",ode=" << ode_name(key.ode)
        << ",linear=" << linear_solver_name(key.linear_solver)
        << ",flux=" << flux_name(key.flux)
        << ",reconstruction=" << reconstruction_name(key.reconstruction)
        << ",integrator=" << integrator_name(key.integrator);
    return out.str();
}

std::size_t RuntimeDispatchRegistry::DispatchKeyHash::operator()(
    const DispatchKey &key) const noexcept
{
    std::size_t seed = 0;
    seed = mix_hash(seed, static_cast<std::size_t>(key.backend));
    seed = mix_hash(seed, static_cast<std::size_t>(key.eos));
    seed = mix_hash(seed, static_cast<std::size_t>(key.network));
    seed = mix_hash(seed, static_cast<std::size_t>(key.ode));
    seed = mix_hash(seed, static_cast<std::size_t>(key.linear_solver));
    seed = mix_hash(seed, static_cast<std::size_t>(key.flux));
    seed = mix_hash(seed, static_cast<std::size_t>(key.reconstruction));
    seed = mix_hash(seed, static_cast<std::size_t>(key.integrator));
    return seed;
}

void RuntimeDispatchRegistry::register_launcher(const DispatchKey &key,
                                                DispatchLauncher launcher,
                                                std::string_view label)
{
    validate_resolved_key(key, "RuntimeDispatchRegistry::register_launcher");
    if (launcher == nullptr) {
        throw std::invalid_argument(
            "Cannot register a null runtime dispatch launcher for "
            + describe_dispatch_key(key));
    }
    if (label.empty()) {
        throw std::invalid_argument(
            "Runtime dispatch launcher label must not be empty for "
            + describe_dispatch_key(key));
    }

    auto [existing, inserted] = entries_.try_emplace(
        key, DispatchEntry{launcher, std::string(label)});
    if (!inserted) {
        throw std::logic_error(
            "Duplicate runtime dispatch registration for "
            + describe_dispatch_key(key)
            + "; existing='" + existing->second.label
            + "', duplicate='" + std::string(label) + "'");
    }
}

const DispatchEntry *RuntimeDispatchRegistry::find(const DispatchKey &key) const noexcept
{
    if (key.backend == ComputeBackend::Auto) {
        return nullptr;
    }
    const auto entry = entries_.find(key);
    return entry == entries_.end() ? nullptr : &entry->second;
}

const DispatchEntry &RuntimeDispatchRegistry::require(const DispatchKey &key) const
{
    validate_resolved_key(key, "RuntimeDispatchRegistry::require");
    if (const DispatchEntry *entry = find(key)) {
        return *entry;
    }

    throw std::runtime_error(
        "No runtime dispatch launcher is registered for "
        + describe_dispatch_key(key)
        + ". This binary does not support the exact requested combination; "
          "no implicit CPU, solver, EOS, or network fallback was applied.");
}

} // namespace arch::runtime
