#include "api/protocol/Response.h"
#include "api/preview/Sampling.h"
#include "api/preview/ResourceEstimates.h"
#include "grid/Grid.h"

#include <iostream>
#include <limits>
#include <stdexcept>

static void require(bool good, const char *message) {
    if (!good) throw std::runtime_error(message);
}
template<class F> static void rejects(F &&f) {
    bool rejected = false;
    try { f(); } catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "invalid sampling must be rejected before allocation");
}
int main() {
    using namespace arch::api;
    using detail::Json;
    PreviewRequest request;
    request.case_id = "CellularDet";
    auto plan = ResolveSampling(request);
    require(plan.nx == 128 && plan.ny == 128 && plan.count == 16384, "2D defaults");
    request.samples_x1 = 256; request.samples_x2 = 256;
    require(ResolveSampling(request).count == 65536, "2D maximum product");
    for (int n : {0, -1, 1, 257, std::numeric_limits<int>::max()}) {
        request.samples_x1 = n;
        rejects([&] { ResolveSampling(request); });
    }
    request.samples_x1 = 2; request.samples_x2 = 3;
    require(ResolveSampling(request).count == 6, "non-square shape");
    request.sample_count_provided = true;
    rejects([&] { ResolveSampling(request); });
    request.sample_count_provided = false;
    request.samples_x2.reset();
    rejects([&] { ResolveSampling(request); });

    PreviewRequest generic;
    generic.case_id = "Gaussian";
    require(ResolveSampling(generic, 1).count == 512, "case-independent 1D plan");
    require(ResolveSampling(generic, 2).count == 16384, "case-independent 2D plan");
    auto volume = ResolveSampling(generic, 3);
    require(volume.nx == 32 && volume.ny == 32 && volume.nz == 32
            && volume.count == 32768 && !volume.two_dimensional, "bounded 3D defaults");
    generic.samples_x1 = 7; generic.samples_x2 = 5; generic.samples_x3 = 3;
    volume = ResolveSampling(generic, 3);
    require(volume.nx == 7 && volume.ny == 5 && volume.nz == 3
            && volume.count == 105, "non-cubic extents and product");
    for (int n : {0, -1, 1, 65, std::numeric_limits<int>::max()}) {
        generic.samples_x3 = n;
        rejects([&] { ResolveSampling(generic, 3); });
    }
    for (int axis = 0; axis < 3; ++axis) {
        generic.samples_x1 = 2; generic.samples_x2 = 2; generic.samples_x3 = 2;
        std::optional<int>* extents[] = {&generic.samples_x1, &generic.samples_x2, &generic.samples_x3};
        for (int n : {1, 65, std::numeric_limits<int>::max()}) {
            *extents[axis] = n;
            rejects([&] { ResolveSampling(generic, 3); });
        }
    }
    generic.samples_x1 = 64; generic.samples_x2 = 64; generic.samples_x3 = 64;
    rejects([&] { ResolveSampling(generic, 3); }); // Legal axes, product over budget.
    generic.samples_x1 = 64; generic.samples_x2 = 32; generic.samples_x3 = 16;
    require(ResolveSampling(generic, 3).count == 32768, "non-cubic total budget boundary");
    generic.samples_x3.reset();
    rejects([&] { ResolveSampling(generic, 3); });
    generic.samples_x3 = 2;
    rejects([&] { ResolveSampling(generic, 2); });
    rejects([&] { ResolveSampling(generic, 1); });
    rejects([&] { ResolveSampling(generic, 0); });
    rejects([&] { ResolveSampling(generic, 4); });
    generic.samples_x1 = 2; generic.samples_x2 = 2; generic.samples_x3 = 2;
    generic.sample_count_provided = true;
    rejects([&] { ResolveSampling(generic, 3); });

    require(RootBlockCount(1, {2, 0, 0}) == 2, "1D inactive axes not counted");
    require(RootBlockCount(2, {2, 3, 0}) == 6, "2D roots");
    require(RootBlockCount(3, {2, 3, 5}) == 30, "3D root capacity includes x3");
    require(!RootBlockCount(3, {2, 3, 0}), "active third axis cannot be zero");
    require(!RootBlockCount(3, {std::numeric_limits<int>::max(),
        std::numeric_limits<int>::max(), std::numeric_limits<int>::max()}),
        "root product overflow before allocation");
    require(!RootBlockCount(0, {1, 1, 1}), "invalid root dimension");

    GridConfig topology;
    topology.dim = 3;
    topology.nblockx1 = 2; topology.nblockx2 = 3; topology.nblockx3 = 5;
    topology.x1_min = 0; topology.x1_max = 2;
    topology.x2_min = -1; topology.x2_max = 3;
    topology.x3_min = 7; topology.x3_max = 11;
    topology.x1l_boundary_type = topology.x1r_boundary_type = "outflow";
    topology.x2l_boundary_type = topology.x2r_boundary_type = "outflow";
    topology.x3l_boundary_type = topology.x3r_boundary_type = "outflow";
    topology.amr_max_blocks = 30;
    AmrConfig levels; levels.lrefinemin = 0; levels.lrefinemax = 0;
    ValidateInitialPreviewGrid(topology, levels);
    topology.amr_max_blocks = 29;
    rejects([&] { ValidateInitialPreviewGrid(topology, levels); });
    topology.amr_max_blocks = 30;
    topology.x3_max = topology.x3_min;
    rejects([&] { ValidateInitialPreviewGrid(topology, levels); });
    topology.x3_max = 11;
    topology.x3_min = std::numeric_limits<double>::infinity();
    rejects([&] { ValidateInitialPreviewGrid(topology, levels); });
    topology.x3_min = 7;
    topology.x3r_boundary_type = "unknown";
    rejects([&] { ValidateInitialPreviewGrid(topology, levels); });
    topology.x3r_boundary_type = "outflow";
    topology.nblockx3 = int(amr::kMortonCoordinateMask);
    levels.lrefinemax = 1;
    rejects([&] { ValidateInitialPreviewGrid(topology, levels); });
    topology.nblockx3 = 5; levels.lrefinemax = 0;
    topology.dim = 4;
    rejects([&] { ValidateInitialPreviewGrid(topology, levels); });

    Grid grid(0, -2, 6, 10, 16, 50, 60);
    grid.dim = 2;
    grid.InitializeTopology();
    const auto point = grid.GetPhysicalCoords(1, 2);
    const auto expanded = Grid::PhysicalCoordsFromNative(2, "cartesian", point.x, point.y, 999);
    require(point.z == 0 && expanded.z == 0 && point.r == expanded.r, "inactive coordinate uses grid authority");

    require(Json("\n").dump(8).size() == 8, "budget counts escaped JSON bytes");
    bool too_large = false;
    try { Json("\n").dump(7); } catch (const std::length_error &) { too_large = true; }
    require(too_large, "writer must reject rather than truncate");
    auto response = Json::object({{"schemaVersion", "1.0"}, {"kind", "initial-state-preview"},
        {"identity", Json::object({{"requestId", "keep-me"}})},
        {"state", Json::object({{"setup", "ready"}})},
        {"data", std::string(max_response_bytes, 'x')}});
    auto result = SerializePreviewResponse(response, 0);
    require(result.exit_code == 7 && result.json.size() + 1 <= max_response_bytes, "bounded error response");
    require(result.json.find("RESPONSE_TOO_LARGE") != std::string::npos, "stable response-size diagnostic");
    require(result.json.find("keep-me") != std::string::npos && result.json.find("ready") != std::string::npos,
            "oversize data retains request and confirmed state");
    require(result.json.find("\"data\":null") != std::string::npos, "no partial success data");
    response["state"] = std::string(max_response_bytes, 'x');
    result = SerializePreviewResponse(response, 0);
    require(result.json.find("STATE_OMITTED_FOR_SIZE") != std::string::npos
            && result.json.find("keep-me") != std::string::npos, "oversize state still retains identity");
    std::cout << "Sampling budgets, shared coordinates and bounded responses passed\n";
}
