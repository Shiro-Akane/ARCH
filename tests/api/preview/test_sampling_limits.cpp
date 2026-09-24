#include "api/protocol/Response.h"
#include "api/preview/Sampling.h"
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
