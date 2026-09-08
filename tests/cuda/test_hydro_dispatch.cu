// Host-side CUDA registry regression: no numerical route/kernel instantiations.
#include "cuda/hydro/HydroIntegratorPolicies.cuh"

#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace arch;

struct ObservedRoute {
    int calls = 0;
    dispatch::FluxId flux{};
    dispatch::ReconstructionId reconstruction{};
    dispatch::LimiterId limiter{};

    template <class Reconstruction, class Flux>
    void operator()()
    {
        ++calls;
        if constexpr (std::is_same_v<Flux, cuda::CudaVlFlux>) flux = dispatch::FluxId::Vl;
        else if constexpr (std::is_same_v<Flux, cuda::CudaSwFlux>) flux = dispatch::FluxId::Sw;
        else if constexpr (std::is_same_v<Flux, cuda::CudaRoeFlux>) flux = dispatch::FluxId::Roe;
        else if constexpr (std::is_same_v<Flux, cuda::CudaHllFlux>) flux = dispatch::FluxId::Hll;
        else if constexpr (std::is_same_v<Flux, cuda::CudaHllcFlux>) flux = dispatch::FluxId::Hllc;
        else static_assert(!sizeof(Flux), "unreviewed Hydro flux binding");

        if constexpr (std::is_same_v<Reconstruction, cuda::CudaPcmReconstruction>)
            reconstruction = dispatch::ReconstructionId::Pcm;
        else if constexpr (std::is_same_v<Reconstruction, cuda::CudaPpmReconstruction>)
            reconstruction = dispatch::ReconstructionId::Ppm;
        else {
            reconstruction = dispatch::ReconstructionId::Muscl;
            if constexpr (std::is_same_v<Reconstruction, cuda::CudaMusclReconstruction<MinMod>>)
                limiter = dispatch::LimiterId::MinMod;
            else if constexpr (std::is_same_v<Reconstruction, cuda::CudaMusclReconstruction<McLimiter>>)
                limiter = dispatch::LimiterId::Mc;
            else if constexpr (std::is_same_v<Reconstruction, cuda::CudaMusclReconstruction<SuperBee>>)
                limiter = dispatch::LimiterId::SuperBee;
            else if constexpr (std::is_same_v<Reconstruction, cuda::CudaMusclReconstruction<VanLeer>>)
                limiter = dispatch::LimiterId::VanLeer;
            else static_assert(!sizeof(Reconstruction), "unreviewed Hydro reconstruction binding");
        }
    }
};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void require_rejected(const dispatch::ResolvedExecutionPlan& plan)
{
    ObservedRoute observed;
    require(!cuda::visit_cuda_hydro_route(plan, observed) && observed.calls == 0,
        "Unregistered Hydro route invoked a callback");
}
} // namespace

int main()
{
    try {
        dispatch::ResolvedExecutionPlan plan{};
        int checked = 0;
        for (const auto flux : dispatch::make_policy_descriptors<dispatch::FluxPolicies>())
            for (const auto reconstruction : dispatch::make_policy_descriptors<dispatch::ReconstructionPolicies>())
                for (const auto limiter : dispatch::make_policy_descriptors<dispatch::LimiterPolicies>()) {
                    plan.flux = flux.id;
                    plan.reconstruction = reconstruction.id;
                    plan.limiter = limiter.id;
                    ObservedRoute observed;
                    require(cuda::visit_cuda_hydro_route(plan, observed), "Registered Hydro route was rejected");
                    require(observed.calls == 1 && observed.flux == plan.flux
                        && observed.reconstruction == plan.reconstruction
                        && (plan.reconstruction != dispatch::ReconstructionId::Muscl
                            || observed.limiter == plan.limiter),
                        "Hydro registry selected the wrong policy or invoked it more than once");
                    ++checked;
                }
        plan.flux = static_cast<dispatch::FluxId>(255);
        require_rejected(plan);
        plan.flux = dispatch::FluxId::Hllc;
        plan.reconstruction = static_cast<dispatch::ReconstructionId>(255);
        require_rejected(plan);
        plan.reconstruction = dispatch::ReconstructionId::Muscl;
        plan.limiter = static_cast<dispatch::LimiterId>(255);
        require_rejected(plan);
        for (const auto reconstruction : {dispatch::ReconstructionId::Pcm, dispatch::ReconstructionId::Ppm}) {
            plan.reconstruction = reconstruction;
            ObservedRoute observed;
            require(cuda::visit_cuda_hydro_route(plan, observed) && observed.calls == 1
                && observed.reconstruction == reconstruction,
                "A limiter-independent reconstruction consulted the MUSCL limiter registry");
        }
        std::cout << "CUDA Hydro registry combinations=" << checked
                  << " unknown-ID controls=3 limiter-independent controls=2 passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
