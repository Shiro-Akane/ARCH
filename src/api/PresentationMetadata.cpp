#include "Configuration.h"
#include "../grid/Grid.h"
#include "../driver/dispatch/PolicyDescriptor.h"

namespace arch::api {
using detail::Json;
std::string UnitSystem(const SimConfig& config) {
    (void)config;
    return "cgs"; // All Core inputs and outputs use CGS, including IdealGas.
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
Json RefinementMetadata(const SimConfig& c) {
    const auto& a = c.amr;
    struct Item { const char* name; bool selected; const char* unavailable; };
    const Item items[] = {{"DENS", a.refine_on_rho, ""}, {"PRES", a.refine_on_p, ""},
        {"TEMP", a.refine_on_temp, ""}, {"VELX", a.refine_on_velx, ""},
        {"VELY", a.refine_on_vely, c.grid.dim < 2 ? "requires at least 2D" : ""},
        {"VELZ", a.refine_on_velz, c.grid.dim < 3 ? "requires 3D" : ""},
        {"ENER", a.refine_on_eng, ""}, {"VORT", a.refine_on_vorticity, ""},
        {"DIVV", a.refine_on_div_v, ""}, {"ENTR", a.refine_on_entropy, ""},
        {"ENUC", a.refine_on_enuc, !c.physics.burn.use_burn ? "requires reactions" : ""},
        {"JENS", a.refine_on_jeans, "self gravity is not implemented"},
        {"SPECIES", a.refine_all_species, ""}};
    auto choices = Json::array();
    for (const auto& item : items)
        choices.push(Json::object({{"value", item.name}, {"selected", item.selected},
            {"available", *item.unavailable == 0}, {"reason", *item.unavailable ? Json(item.unavailable) : Json()}}));
    auto names = Json::array();
    for (const auto& name : a.refine_species_names) names.push(name);
    return Json::object({{"choices", choices}, {"namedSpecies", names},
        {"speciesResolution", "requires case Setup"}, {"separator", ","}, {"alternativeSeparator", "+"}});
}
Json DiffusionMetadata(const SimConfig& c) {
    const bool helm = dispatch::ascii_iequals(c.physics.eos_type, "helmholtz");
    const bool ideal = dispatch::ascii_iequals(c.physics.eos_type, "ideal");
    auto channels = Json::array();
    const char* toggles[] = {"use_thermal_diff", "use_viscous_diff", "use_species_diff"};
    const char* coefficients[] = {"alpha_therm", "nu_visc", "D_spec"};
    for (int i = 0; i < 3; ++i)
        channels.push(Json::object({{"toggleKey", toggles[i]}, {"coefficientKey", coefficients[i]},
            {"unit", "cm^2/s"}, {"constantInputAllowed", !helm},
            {"stellarModelSuppliesCoefficient", i == 0}}));
    return Json::object({{"version", "1"}, {"enabled", c.physics.diffusion.use_diffusion},
        {"modeEditable", false}, {"source", ideal ? "constant" : "state-dependent"},
        {"sourceScope", "configuration; EOS state not evaluated"},
        {"possibleSources", ideal ? Json::array({"constant"}) : Json::array({"constant", "stellar-conductivity"})},
        {"selectionRule", "species_count > 0 and EOS electron density > 0 selects stellar transport; positive viscosity/thermal overrides then fail"},
        {"forbiddenExplicitKeys", helm && c.physics.diffusion.use_diffusion ? Json::array({"alpha_therm", "nu_visc", "D_spec"}) : Json::array()},
        {"channels", channels}, {"conflictResolution", "explicit reversible removal from working text; hiding fields is insufficient"}});
}
} // namespace arch::api
