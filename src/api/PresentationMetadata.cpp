#include "Configuration.h"
#include "../grid/Grid.h"
#include "../driver/dispatch/PolicyDescriptor.h"

namespace arch::api {
using detail::Json;
std::string UnitSystem(const SimConfig& config) {
    const auto& eos = config.physics.eos_type;
    if (dispatch::ascii_iequals(eos, "helmholtz") || dispatch::ascii_iequals(eos, "tabular"))
        return "cgs";
    // The ideal closure accepts model-supplied Cv and arbitrary consistent units.
    if (dispatch::ascii_iequals(eos, "ideal")) return "code";
    return "unknown";
}
std::string FieldUnit(const std::string& key, const std::string& system) {
    if (system == "unknown") return {};
    const bool cgs = system == "cgs";
    if (key == "DENS") return cgs ? "g/cm^3" : "code_density";
    if (key == "TEMP") return cgs ? "K" : "code_temperature";
    if (key == "PRES") return cgs ? "erg/cm^3" : "code_pressure";
    if (key == "ENER") return cgs ? "erg/cm^3" : "code_energy_density";
    if (key == "EINT") return cgs ? "erg/g" : "code_specific_energy";
    if (key == "VELX" || key == "VELY" || key == "VELZ") return cgs ? "cm/s" : "code_velocity";
    return {};
}
std::string AxisUnit(const std::string& label, const std::string& system) {
    if (label == "phi" || label == "phi_cy" || label == "theta") return "rad";
    if (system == "cgs") return "cm";
    if (system == "code") return "code_length";
    return {};
}
Json CoordinateMetadata(const GridConfig& g, const std::string& system) {
    Grid grid;
    grid.geometry = g.geometry;
    grid.dim = g.dim;
    const auto names = grid.GetAxisNames();
    const bool known_geometry = dispatch::parse_geometry(g.geometry).ok;
    const int blocks[] = {g.nblockx1, g.nblockx2, g.nblockx3};
    auto axes = Json::array();
    for (int i = 0; i < 3; ++i) {
        const std::string raw = "x" + std::to_string(i+1);
        const bool active = i < g.dim;
        const auto label = active && known_geometry ? names.at(i) : raw;
        const bool angular = label == "theta" || label == "phi" || label == "phi_cy";
        const auto unit = active && known_geometry ? AxisUnit(label, system) : std::string();
        const auto display = label.ends_with("_cy") ? label.substr(0, label.size()-3) : label;
        axes.push(Json::object({{"key", raw}, {"displayName", display}, {"nativeName", label}, {"active", active},
            {"kind", !active ? "inactive" : !known_geometry ? "unknown" : angular ? "angle" : "length"},
            {"unit", unit.empty() ? Json() : Json(unit)},
            {"blocksKey", "nblock" + raw}, {"blocks", blocks[i]},
            {"minKey", raw+"_min"}, {"maxKey", raw+"_max"},
            {"lowerBoundaryKey", raw+"l_boundary_type"},
            {"upperBoundaryKey", raw+"r_boundary_type"}}));
    }
    return Json::object({{"geometry", g.geometry}, {"dimension", g.dim},
        {"axes", axes}, {"activation", "positive-block-count"}, {"disabledBlockCount", 0},
        {"thirdAxisRequiresSecond", true}, {"unitSystem", system}});
}
} // namespace arch::api
