/**
 * @file MaterialValue.h
 * @brief Host-only provenance for material registration, outside numeric views.
 */
#pragma once
#include <string>
#include <utility>

namespace arch::config {
enum class MaterialOrigin { Unknown, ModelDefinition, ResolvedInput, NetworkTable, NetworkDefinition };
struct MaterialValue {
    double value = 0;
    MaterialOrigin origin = MaterialOrigin::Unknown;
    std::string owner;
    std::string source_identity;
    std::string parameter_key;

    static MaterialValue NetworkConstant(double value, const std::string& network,
                                         const std::string& field) {
        return {value, MaterialOrigin::NetworkDefinition, "network:" + network + ":" + field, {}, {}};
    }

    static MaterialValue Network(double value, const std::string& network,
                                 const std::string& field) {
        // Names the compiled table owner. Exact package/build identity remains
        // the build manifest's responsibility, not an invented table hash.
        return {value, MaterialOrigin::NetworkTable, "network:" + network + ":" + field, {}, {}};
    }
};
} // namespace arch::config
