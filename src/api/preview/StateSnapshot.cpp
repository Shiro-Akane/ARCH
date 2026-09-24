/**
 * @file StateSnapshot.cpp
 * @brief Materialize only requested state fields from a preview mesh.
 *
 * Workflow:
 * 1. Accept a bounded, verified request at the read-only API boundary.
 * 2. Materialize only requested state fields from a preview mesh.
 * 3. Return typed evidence or an explicit error; do not start the simulation Driver.
 */

#include "api/preview/StateSnapshot.h"

#include "driver/dispatch/PolicyDescriptor.h"
#include "grid/Grid.h"

namespace arch::api {
using detail::Json;
namespace {
/** Serialize resolved root-grid geometry and units. */
Json grid_snapshot(const SimConfig &config) {
    const auto &g = config.grid;
    const int blocks[] = {g.nblockx1, g.nblockx2, g.nblockx3};
    const int cells[] = {amr::BLOCK_NX, amr::BLOCK_NY, amr::BLOCK_NZ};
    const double lo[] = {g.x1_min, g.x2_min, g.x3_min};
    const double hi[] = {g.x1_max, g.x2_max, g.x3_max};
    const std::string lower[] = {g.x1l_boundary_type, g.x2l_boundary_type, g.x3l_boundary_type};
    const std::string upper[] = {g.x1r_boundary_type, g.x2r_boundary_type, g.x3r_boundary_type};
    Grid native; native.geometry = g.geometry; native.dim = g.dim;
    const auto names = native.GetAxisNames();
    auto axes = Json::array();
    for (int axis = 0; axis < g.dim; ++axis) {
        const std::int64_t n = std::int64_t(blocks[axis]) * cells[axis];
        const bool known = dispatch::parse_geometry(g.geometry).ok;
        const auto label = known ? names.at(axis) : "x"+std::to_string(axis+1);
        const auto unit = known ? AxisUnit(label, UnitSystem(config)) : std::string();
        axes.push(Json::object({{"name", "x" + std::to_string(axis + 1)}, {"unit", unit.empty() ? Json() : Json(unit)},
            {"displayName", label.ends_with("_cy") ? label.substr(0, label.size()-3) : label},
            {"min", lo[axis]}, {"max", hi[axis]}, {"rootBlocks", blocks[axis]},
            {"activeCellsPerBlock", cells[axis]}, {"rootCells", n},
            {"coordinateSpacing", n > 0 ? Json((hi[axis] - lo[axis]) / n) : Json()},
            {"lowerBoundary", lower[axis]}, {"upperBoundary", upper[axis]}}));
    }
    return Json::object({{"status", "configured"}, {"geometry", g.geometry}, {"dimension", g.dim},
        {"axes", axes}, {"hierarchy", "not_constructed"}});
}
/** Serialize AMR level limits and capacity from configuration. */
Json amr_snapshot(const SimConfig &config) {
    const auto &a = config.amr;
    auto indicators = Json::array();
    const std::pair<const char *, bool> flags[] = {
        {"DENS", a.refine_on_rho}, {"PRES", a.refine_on_p}, {"TEMP", a.refine_on_temp},
        {"VELX", a.refine_on_velx}, {"VELY", a.refine_on_vely}, {"VELZ", a.refine_on_velz},
        {"ENER", a.refine_on_eng}, {"VORT", a.refine_on_vorticity}, {"DIVV", a.refine_on_div_v},
        {"ENTR", a.refine_on_entropy}, {"ENUC", a.refine_on_enuc}, {"JENS", a.refine_on_jeans}};
    for (const auto &[key, active] : flags) if (active) indicators.push(key);
    if (a.refine_on_species) {
        if (a.refine_all_species) indicators.push("SPECIES");
        else for (const auto &name : a.refine_species_names) indicators.push(name);
    }
    return Json::object({{"status", "configured"}, {"enabled", a.lrefinemax > 0},
        {"minLevel", a.lrefinemin}, {"maxLevel", a.lrefinemax},
        {"requestedIndicators", a.refine_var}, {"parsedIndicators", indicators},
        {"indicatorEvaluation", "not_executed"},
        {"refineThreshold", a.refine_threshold}, {"derefineThreshold", a.derefine_threshold},
        {"regridInterval", a.regrid_interval}, {"maxBlocks", config.grid.amr_max_blocks},
        {"effectiveMaxBlocks", config.grid.amr_max_blocks > 0 ? config.grid.amr_max_blocks : 10000},
        {"initialRefinement", "not_executed"}, {"actualHierarchy", Json()}});
}
/** Serialize selected EOS identity and source paths. */
Json eos_snapshot(const SimConfig &config) {
    return Json::object({{"status", "not_loaded"}, {"requested", config.physics.eos_type},
        {"resolved", Json()}, {"configuredGamma", config.physics.gamma},
        {"tablePath", config.physics.eos_table_path},
        {"componentTablePath", config.physics.eos_helm_table_path},
        {"loadedTablePath", Json()}, {"sourceFingerprint", Json()}});
}
}
/** Refresh the configuration snapshot after case setup may change effective values. */
void PublishStateSnapshot(Json &state, const SimConfig &config) {
    state["configuration"] = "parsed";
    state["units"] = Json::object({{"system", UnitSystem(config)}, {"basis", "core-cgs-contract"}, {"valuesConverted", false}});
    state["coordinates"] = CoordinateMetadata(config.grid, UnitSystem(config));
    state["grid"] = grid_snapshot(config);
    state["amr"] = amr_snapshot(config);
    state["eos"] = eos_snapshot(config);
    state["computeBackendRequested"] = config.execution.compute_backend;
    state["diffusion"] = DiffusionMetadata(config);
    state["amrIndicators"] = RefinementMetadata(config);
}
/** Serialize the final ordered species registry. */
Json SpeciesSnapshot(const SpeciesManager &specs) {
    auto species = Json::array();
    for (int i = 0; i < specs.count(); ++i)
        species.push(Json::object({{"index", i}, {"name", specs.get_name(i)}}));
    return species;
}

} // namespace arch::api
