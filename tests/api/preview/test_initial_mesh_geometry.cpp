#include "api/preview/InitialMesh.h"
#include "physics/eos/IdealGas.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

// Geometry fixture only: no scientific model formula or simulation Driver.
class GeometryFixture final : public ProblemGenerator {
public:
    int initializations = 0;
    void Setup(SimConfig&, SpeciesManager&) override {
        throw std::logic_error("Geometry fixture does not invoke model Setup");
    }
    void InitializeData(amr::AMRControl& control, const SimConfig&,
                        const SpeciesManager&, ProblemInitializationContext) override {
        ++initializations;
        for (int id : control.tree->GetActiveBlocks()) {
            auto& state = control.pool->GetBlock(id).fluid_state;
            std::fill(state.rho.begin(), state.rho.end(), 1.0);
            std::fill(state.eng.begin(), state.eng.end(), 2.5);
        }
    }
};
static void require(bool good, const char* message) {
    if (!good) throw std::runtime_error(message);
}
int main() {
    using namespace arch::api;
    SimConfig config;
    config.grid.dim = 3; config.grid.geometry = "cartesian";
    config.grid.nblockx1 = 1; config.grid.nblockx2 = 1; config.grid.nblockx3 = 2;
    config.grid.x1_min = -2; config.grid.x1_max = 6;
    config.grid.x2_min = 10; config.grid.x2_max = 16;
    config.grid.x3_min = 7; config.grid.x3_max = 11;
    config.grid.amr_max_blocks = 2;
    config.amr.lrefinemin = 0; config.amr.lrefinemax = 0;
    ValidateInitialPreviewGrid(config.grid, config.amr);
    SpeciesManager species;
    species.add_species("geometry-fixture", 1, 1, 1.4, 1.0);
    IdealGas eos(1.4, species);
    GeometryFixture problem;
    PreviewRequest request;
    request.mesh_max_blocks = 1; request.mesh_memory_mib = 128;
    const auto limited = BuildInitialMesh(problem, config, species,
        arch::dispatch::EosId::Ideal, eos, request);
    require(!limited.constructed && !limited.complete && problem.initializations == 0,
        "third-axis roots must be rejected before allocation/initialization");
    require(limited.data.dump().find("root-grid-exceeds-working-capacity") != std::string::npos,
        "capacity refusal is explicit");
    request.mesh_max_blocks = 2;
    const auto mesh = BuildInitialMesh(problem, config, species,
        arch::dispatch::EosId::Ideal, eos, request);
    require(mesh.constructed && mesh.complete && problem.initializations == 1,
        "construct real root topology with two third-axis leaves");
    const auto json = mesh.data.dump();
    require(json.find("\"leafCount\":2") != std::string::npos, "actual 3D leaf count");
    require(json.find("\"logicalKey\":\"0:0:0:1\"") != std::string::npos, "third-axis identity");
    require(json.find("\"lower\":[-2,10,7]") != std::string::npos
        && json.find("\"upper\":[6,16,9]") != std::string::npos,
        "first third-axis slab bounds");
    require(json.find("\"lower\":[-2,10,9]") != std::string::npos
        && json.find("\"upper\":[6,16,11]") != std::string::npos,
        "second third-axis slab bounds");
    require(json.find("\"cellShape\":[16,16,16]") != std::string::npos
        && json.find("\"cellSpacing\":[0.5,0.375,0.125]") != std::string::npos,
        "three-axis cell geometry");
    require(json.find("\"rootBlocks\":2") != std::string::npos
        && json.find("\"activeCells\":8192") != std::string::npos,
        "resource metadata agrees with actual three-axis topology");
    config.grid.nblockx1 = config.grid.nblockx2 = config.grid.nblockx3 =
        std::numeric_limits<int>::max();
    bool overflow_rejected = false;
    try { (void)BuildInitialMesh(problem, config, species,
        arch::dispatch::EosId::Ideal, eos, request); }
    catch (const std::invalid_argument&) { overflow_rejected = true; }
    require(overflow_rejected && problem.initializations == 1,
        "root overflow is an error before allocation, not a completed/limited mesh");
    std::cout << "Real initial root topology: third-axis budget, identity and geometry passed\n";
}
