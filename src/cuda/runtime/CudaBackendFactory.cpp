/**
 * @file CudaBackendFactory.cpp
 * @brief Construct the CUDA backend and bind a selected shared EOS view.
 *
 * Validate typed EOS identity against the resolved plan, create runtime storage
 * and immutable owners, then finish uploads before returning the backend.
 * CudaBackend::Impl owns the stream and resources; the physical EOS still lives
 * in its shared policy, independent of host/device table placement.
 */

#include "cuda/runtime/control/CudaBackendInternal.h"

namespace arch::cuda {
namespace {

constexpr dispatch::EosId host_eos_id(const IdealGas&) noexcept
{
    return dispatch::EosId::Ideal;
}

constexpr dispatch::EosId host_eos_id(const HelmEos&) noexcept
{
    return dispatch::EosId::Helmholtz;
}

constexpr dispatch::EosId host_eos_id(
    const Tabular3DEOSHostView&) noexcept
{
    return dispatch::EosId::Tabular3D;
}

constexpr dispatch::EosId host_eos_id(
    const Tabular4DEOSHostView&) noexcept
{
    return dispatch::EosId::Tabular4D;
}

} // namespace

void CudaBackend::Impl::initialize_eos(
    const IdealGas& host, const SpeciesManager& species)
{
    if (!std::holds_alternative<std::monostate>(eos))
        throw std::logic_error("CUDA EOS owner already exists");
    species_owner = std::make_unique<DeviceSpeciesOwner>(
        species, stream.get());
    species_view = species_owner->view();
    eos = species_owner->ideal_gas_view(host.global_gamma);
    finish_eos_upload("upload Ideal EOS");
    ++immutable_owner_constructions;
}

void CudaBackend::Impl::initialize_eos(
    const HelmEos& host, const SpeciesManager&)
{
    if (!std::holds_alternative<std::monostate>(eos))
        throw std::logic_error("CUDA EOS owner already exists");
    helm_owner = std::make_unique<HelmEosDeviceOwner>(host, stream.get());
    const HelmEosView view = helm_owner->view();
    species_view = view.specs;
    eos = view;
    finish_eos_upload("upload Helm EOS");
    ++immutable_owner_constructions;
}

void CudaBackend::Impl::initialize_eos(
    const Tabular3DEOSHostView& host, const SpeciesManager&)
{
    if (!std::holds_alternative<std::monostate>(eos))
        throw std::logic_error("CUDA EOS owner already exists");
    tabular3_owner = std::make_unique<Tabular3DEOSDeviceOwner>(
        host, stream.get());
    const Tabular3DEOSView view = tabular3_owner->view();
    species_view = view.specs;
    eos = view;
    finish_eos_upload("upload Tabular3 EOS");
    ++immutable_owner_constructions;
}

void CudaBackend::Impl::initialize_eos(
    const Tabular4DEOSHostView& host, const SpeciesManager&)
{
    if (!std::holds_alternative<std::monostate>(eos))
        throw std::logic_error("CUDA EOS owner already exists");
    tabular4_owner = std::make_unique<Tabular4DEOSDeviceOwner>(
        host, stream.get());
    const Tabular4DEOSView view = tabular4_owner->view();
    species_view = view.specs;
    eos = view;
    finish_eos_upload("upload Tabular4 EOS");
    ++immutable_owner_constructions;
}

void CudaBackend::Impl::finish_eos_upload(const char* operation)
{
    checked_quiesce(operation);
}

template <class Eos>
std::unique_ptr<CudaBackend> make_cuda_backend_impl(
    std::span<const CudaBlockBinding> blocks, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const Eos& eos)
{
    if (launch.plan.eos != host_eos_id(eos))
        throw std::invalid_argument(
            "resolved EOS does not match the CUDA factory owner");
    auto implementation = std::make_unique<CudaBackend::Impl>(
        blocks, device_ordinal, launch, species);
    implementation->initialize_eos(eos, species);
    return std::make_unique<CudaBackend>(std::move(implementation));
}

template <class Eos>
std::unique_ptr<CudaBackend> make_single_cuda_backend_impl(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary, const Eos& eos)
{
    const std::array<CudaBlockBinding, 1> blocks{{
        {&block, handle, storage, &boundary}}};
    return make_cuda_backend_impl(
        std::span<const CudaBlockBinding>(blocks), device_ordinal, launch,
        species, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary, const IdealGas& eos)
{
    return make_single_cuda_backend_impl(
        block, handle, storage, device_ordinal, launch, species, boundary, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary, const HelmEos& eos)
{
    return make_single_cuda_backend_impl(
        block, handle, storage, device_ordinal, launch, species, boundary, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary,
    const Tabular3DEOSHostView& eos)
{
    return make_single_cuda_backend_impl(
        block, handle, storage, device_ordinal, launch, species, boundary, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    const amr::Block& block, amr::BlockHandle handle,
    backend::StorageGeneration storage, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const boundary::BoundaryPlan& boundary,
    const Tabular4DEOSHostView& eos)
{
    return make_single_cuda_backend_impl(
        block, handle, storage, device_ordinal, launch, species, boundary, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    std::span<const CudaBlockBinding> blocks, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const IdealGas& eos)
{
    return make_cuda_backend_impl(
        blocks, device_ordinal, launch, species, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    std::span<const CudaBlockBinding> blocks, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const HelmEos& eos)
{
    return make_cuda_backend_impl(
        blocks, device_ordinal, launch, species, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    std::span<const CudaBlockBinding> blocks, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const Tabular3DEOSHostView& eos)
{
    return make_cuda_backend_impl(
        blocks, device_ordinal, launch, species, eos);
}

std::unique_ptr<CudaBackend> make_cuda_backend(
    std::span<const CudaBlockBinding> blocks, int device_ordinal,
    const CudaLaunchConfig& launch, const SpeciesManager& species,
    const Tabular4DEOSHostView& eos)
{
    return make_cuda_backend_impl(
        blocks, device_ordinal, launch, species, eos);
}


} // namespace arch::cuda
