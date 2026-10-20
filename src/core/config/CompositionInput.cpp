/**
 * @file CompositionInput.cpp
 * @brief Query the selected CPU network's species without initializing a case.
 */
#include "core/config/CompositionInput.h"
#include "numerics/burnsolver/Networks.h"

namespace arch::config {
CompositionDeclaration DescribeNetworkComposition(const StandardInputResolution& inputs) {
    CompositionDeclaration result;
    const auto* name = input_detail::get<std::string>(inputs, "network_name");
    if (!name) return result;
    using namespace arch::dispatch;
    const auto selected = parse_registered_policy<NetworkPolicies>(*name);
    if (!selected.ok) return result;
    result.network = *name;
    if (selected.value == NetworkId::None) {
        result.complete = true;
        return result;
    }
    visit_policy<NetworkPolicies>(selected.value, [&]<class Registration> {
        using Binding = typename PolicyRegistration<Registration>::CpuBinding;
        if constexpr (!std::is_same_v<Binding, AbsentBinding>
                      && !std::is_same_v<Binding, CpuNoNetworkBinding>) {
            using Network = typename CpuNetworkType<Binding>::type;
            if constexpr (requires { Network::SPECIES_NAMES; }) {
                for (const auto name : Network::SPECIES_NAMES)
                    result.keys.push_back(CompositionKey("x" + std::string(name)));
                result.complete = true;
            }
        }
    });
    return result;
}
} // namespace arch::config
