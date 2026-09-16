/**
 * @file CudaBackendBurnSparseFactory.cpp
 * @brief Resolve registered network/EOS bindings to sparse burn owners.
 *
 * This host-only factory checks the selected provider and requested capacity,
 * then delegates allocation to a typed owner. It does not instantiate ODE kernels
 * or provide an alternative network or linear-solver algorithm.
 */
#include "cuda/runtime/burn/CudaBackendBurnSparseRoutes.h"
#include "physics/eos/IdealGas.h"
#include "physics/eos/HelmEos.h"
#include "physics/eos/Tabular3DEOS.h"
#include "physics/eos/Tabular4DEOS.h"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace arch::cuda {
namespace {

#if ARCH_HAS_CUDSS_PROVIDER
template<class Eos>
struct SparseNetworkOwnerVisitor {
    const dispatch::ResolvedExecutionPlan& plan;
    Eos eos;
    int max_cells;
    cudaStream_t stream;
    std::unique_ptr<CudaSparseBurnOwner> owner;

    template<class Registration> void operator()()
    {
        using Binding = typename dispatch::PolicyRegistration<Registration>::CudaBinding;
        if constexpr (!std::is_same_v<Binding, dispatch::AbsentBinding>
                      && !std::is_same_v<Binding, dispatch::CudaNoNetworkBinding>) {
            owner = burn_detail::make_sparse_burn_owner_route<Binding, Eos>(
                plan, eos, max_cells, stream);
        }
    }
};
#endif

template<class Eos>
std::unique_ptr<CudaSparseBurnOwner> resolve_sparse_owner(
    const dispatch::ResolvedExecutionPlan& plan, Eos eos,
    int max_cells, cudaStream_t stream)
{
    if (plan.linear_solver != dispatch::LinearSolverId::CuDss)
        throw std::invalid_argument("Sparse CUDA burn owner requires a resolved cuDSS policy");
    if (max_cells <= 0)
        throw std::invalid_argument("Sparse CUDA burn owner requires active cells");
#if ARCH_HAS_CUDSS_PROVIDER
    SparseNetworkOwnerVisitor<Eos> visitor{plan, eos, max_cells, stream, {}};
    if (!dispatch::visit_policy<dispatch::NetworkPolicies>(plan.network, visitor)
        || !visitor.owner)
        throw std::invalid_argument("Sparse CUDA burn owner has no registered CUDA network binding");
    return std::move(visitor.owner);
#else
    static_cast<void>(eos);
    static_cast<void>(stream);
    throw std::runtime_error("cuDSS was selected, but this ARCH build has no CUDA cuDSS provider");
#endif
}

} // namespace

#define ARCH_DEFINE_SPARSE_BURN_FACTORY(EOS) \
    std::unique_ptr<CudaSparseBurnOwner> make_cuda_sparse_burn_owner( \
        const dispatch::ResolvedExecutionPlan& plan, EOS eos, \
        int max_cells, cudaStream_t stream) \
    { return resolve_sparse_owner(plan, eos, max_cells, stream); }
ARCH_DEFINE_SPARSE_BURN_FACTORY(IdealGasView)
ARCH_DEFINE_SPARSE_BURN_FACTORY(HelmEosView)
ARCH_DEFINE_SPARSE_BURN_FACTORY(Tabular3DEOSView)
ARCH_DEFINE_SPARSE_BURN_FACTORY(Tabular4DEOSView)
#undef ARCH_DEFINE_SPARSE_BURN_FACTORY

} // namespace arch::cuda
