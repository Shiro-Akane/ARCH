// Host-side CUDA registry and EOS-status regression: no numerical route/kernel instantiations.
#include "cuda/hydro/policies/HydroIntegratorPolicies.cuh"
#include "cuda/hydro/policies/CheckedHydroEos.cuh"

#include <cmath>
#include <iostream>
#include <limits>
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

// Compilation must prove that an excluded family never instantiates a callback;
// runtime checks verify that both compiler families cover the original registry.
template <int Family>
struct ObservedFamilyRoute : ObservedRoute {
    template <class Reconstruction, class Flux>
    void operator()()
    {
        constexpr bool hll_family = std::is_same_v<Flux, cuda::CudaHllFlux>
            || std::is_same_v<Flux, cuda::CudaHllcFlux>;
        static_assert(Family == 1 ? !hll_family : hll_family,
            "Excluded flux family instantiated a callback");
        ObservedRoute::template operator()<Reconstruction, Flux>();
    }
};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

/** Return an actual failed query and exercise the raw EOS status attachment. */
struct FailingStatusEos {
    int* device_error_status = nullptr;

    ARCH_INLINE double failure() const
    {
        if (device_error_status) *device_error_status = 1;
        return std::numeric_limits<double>::quiet_NaN();
    }
    ARCH_INLINE double get_pressure(const FluidVector&, const double*) const
    { return failure(); }
    ARCH_INLINE double get_pressure_from_rho_e(double, double, const double*) const
    { return failure(); }
    ARCH_INLINE double get_dp_drho_e(double, double, const double*) const
    { return failure(); }
    ARCH_INLINE double get_dp_de_rho(double, double, const double*) const
    { return failure(); }
    ARCH_INLINE double get_total_energy_primitive(
        double, double, double, double, double, const double*) const
    { return failure(); }
    ARCH_INLINE void get_dp_drho_e_and_dp_de_rho(
        double, double, const double*, double& chi, double& kappa) const
    { chi = failure(); kappa = failure(); }
};

/** Two real checked layers must isolate optional queries and retain required faults. */
void verify_nested_optional_eos_status()
{
    int inner_status = 0, outer_status = 0;
    const auto inner = cuda::make_checked_hydro_eos(FailingStatusEos{}, &inner_status);
    const auto outer = cuda::make_checked_hydro_eos(inner, &outer_status);
    const FluidVector state{1., 0., 0., 0., 1.};
    const auto required_pressure = [&] { return std::isnan(outer.get_pressure(state, nullptr)); };
    const auto check_optional = [&](auto optional_query, auto required_query, const char* message) {
        inner_status = outer_status = 0;
        require(optional_query(), "Optional checked EOS changed its failed query result");
        require(inner_status == 0 && outer_status == 0, message);
        require(required_query() && outer.required_query_failed(),
            "Nested required EOS query failed to set its owning latch");
        const int saved_inner = inner_status, saved_outer = outer_status;
        require(optional_query(), "Optional checked EOS changed its result after a required failure");
        require(inner_status == saved_inner && outer_status == saved_outer,
            "Optional checked EOS cleared or changed an existing required fault");
    };
    check_optional([&] { return std::isnan(outer.candidate_view().get_pressure(state, nullptr)); },
        required_pressure, "Nested candidate pressure poisoned a required EOS latch");
    check_optional([&] { return std::isnan(outer.probe_pressure_from_rho_e(1., 1., nullptr)); },
        [&] { return std::isnan(outer.get_pressure_from_rho_e(1., 1., nullptr)); },
        "Nested pressure probe poisoned a required EOS latch");
    check_optional([&] { return std::isnan(outer.probe_dp_drho_e(1., 1., nullptr)); },
        [&] { return std::isnan(outer.get_dp_drho_e(1., 1., nullptr)); },
        "Nested density derivative probe poisoned a required EOS latch");
    check_optional([&] { return std::isnan(outer.probe_dp_de_rho(1., 1., nullptr)); },
        [&] { return std::isnan(outer.get_dp_de_rho(1., 1., nullptr)); },
        "Nested energy derivative probe poisoned a required EOS latch");
    check_optional([&] { return std::isnan(outer.probe_total_energy_primitive(1., 0., 0., 0., 1., nullptr)); },
        [&] { return std::isnan(outer.get_total_energy_primitive(1., 0., 0., 0., 1., nullptr)); },
        "Nested total-energy probe poisoned a required EOS latch");
    check_optional([&] {
        double chi = 0., kappa = 0.;
        outer.get_dp_drho_e_and_dp_de_rho(1., 1., nullptr, chi, kappa);
        return std::isnan(chi) && std::isnan(kappa);
    }, required_pressure, "Nested optional derivative pair poisoned a required EOS latch");
    std::cout << "CUDA_CHECKED_HYDRO_EOS_OPTIONAL_STATUS_PASS nested=2 optional_paths=6\n";
}

void require_rejected(const dispatch::ResolvedExecutionPlan& plan)
{
    ObservedRoute observed;
    require(!cuda::visit_cuda_hydro_route(plan, observed) && observed.calls == 0,
        "Unregistered Hydro route invoked a callback");
    ObservedFamilyRoute<1> first;
    ObservedFamilyRoute<2> second;
    require(!cuda::visit_cuda_hydro_route<1>(plan, first) && first.calls == 0
        && !cuda::visit_cuda_hydro_route<2>(plan, second) && second.calls == 0,
        "Unregistered Hydro route entered a compiler family");
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
                    ObservedFamilyRoute<1> first;
                    ObservedFamilyRoute<2> second;
                    const bool in_first = cuda::visit_cuda_hydro_route<1>(plan, first);
                    const bool in_second = cuda::visit_cuda_hydro_route<2>(plan, second);
                    const auto& selected = in_first
                        ? static_cast<const ObservedRoute&>(first)
                        : static_cast<const ObservedRoute&>(second);
                    require(in_first != in_second && first.calls + second.calls == 1
                        && selected.flux == observed.flux
                        && selected.reconstruction == observed.reconstruction
                        && selected.limiter == observed.limiter,
                        "Compiler families changed, repeated or omitted a registered route");
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
            ObservedFamilyRoute<2> grouped;
            require(cuda::visit_cuda_hydro_route<2>(plan, grouped) && grouped.calls == 1
                && grouped.reconstruction == reconstruction,
                "Compiler family consulted an unused MUSCL limiter");
        }
        verify_nested_optional_eos_status();
        std::cout << "CUDA Hydro registry combinations=" << checked
                  << " unknown-ID controls=3 limiter-independent controls=2 passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
