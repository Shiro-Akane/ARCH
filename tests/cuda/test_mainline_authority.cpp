#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "core/ArchPortability.h"
#include "core/ProblemHelper.h"
#include "core/RuntimeParams.h"
#include "driver/DriverControl.h"
#include "driver/dispatch/PolicyDescriptor.h"
#include "io/ConfigParser.h"
#include "physics/diffusionCoe/diffusion_math.hpp"
#include "physics/eos/eos_Utils.h"

namespace {

constexpr const char* kCanonicalHelmPath =
    "EOS_toolkit/tables/helmholtz/helm_table.dat";
constexpr const char* kRetiredHelmPath = "EOS_toolkit/eos_tabular/helmholtz/helm_table.dat";

ARCH_INLINE int ordinary_cxx_portability_witness(int value)
{
    return value + 7;
}

ARCH_FORCEINLINE int ordinary_cxx_forceinline_witness(int value)
{
    return value * 2;
}

std::uint64_t process_id()
{
#if defined(_WIN32)
    return static_cast<std::uint64_t>(_getpid());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

class TempFixture {
public:
    TempFixture()
    {
        static std::atomic<std::uint64_t> nonce{0};
        const auto seed = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        const auto base = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt != 128; ++attempt) {
            const auto serial = nonce.fetch_add(1, std::memory_order_relaxed);
            path_ = base / ("arch-mainline-authority-" + std::to_string(process_id()) +
                            "-" + std::to_string(seed) + "-" + std::to_string(serial));
            if (std::filesystem::create_directory(path_)) {
                return;
            }
        }
        throw std::runtime_error("could not atomically create authority fixture directory");
    }

    TempFixture(const TempFixture&) = delete;
    TempFixture& operator=(const TempFixture&) = delete;
    TempFixture(TempFixture&& other) noexcept : path_(std::move(other.path_)) { other.path_.clear(); }
    TempFixture& operator=(TempFixture&& other) noexcept
    {
        if (this != &other) {
            cleanup();
            path_ = std::move(other.path_);
            other.path_.clear();
        }
        return *this;
    }
    ~TempFixture() { cleanup(); }

    const std::filesystem::path& path() const { return path_; }

private:
    void cleanup() noexcept
    {
        if (!path_.empty()) {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
            path_.clear();
        }
    }
    std::filesystem::path path_;
};

[[noreturn]] void fail(const std::string& message)
{
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message)
{
    if (!condition) {
        fail(message);
    }
}

SimConfig load_fixture_config(
    const TempFixture& fixture, const std::string& name,
    const std::string& contents)
{
    const auto path = fixture.path() / name;
    {
        std::ofstream output(path);
        require(output.good(), "could not create runtime-parameter fixture");
        output << contents;
        require(output.good(), "could not write runtime-parameter fixture");
    }
    return RuntimeParams::Load(path.string());
}

bool same_requirements(
    const arch::dispatch::ExecutionRequirements& left,
    const arch::dispatch::ExecutionRequirements& right)
{
    return left.dimension == right.dimension
        && left.root_blocks_x1 == right.root_blocks_x1
        && left.root_blocks_x2 == right.root_blocks_x2
        && left.root_blocks_x3 == right.root_blocks_x3
        && left.geometry == right.geometry
        && left.uniform_multiblock == right.uniform_multiblock
        && left.amr == right.amr
        && left.gravity == right.gravity
        && left.restart == right.restart
        && left.burn == right.burn
        && left.diffusion == right.diffusion
        && left.use_nse == right.use_nse
        && left.thermal_diffusion == right.thermal_diffusion
        && left.species_diffusion == right.species_diffusion
        && left.viscous_diffusion == right.viscous_diffusion
        && left.species_count == right.species_count
        && left.required_ghost_depth == right.required_ghost_depth
        && left.state_layout == right.state_layout
        && left.boundary_features == right.boundary_features;
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path);
    require(input.good(), "missing authority file: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void require_contains(const std::filesystem::path& path, const std::string& needle)
{
    require(read_file(path).find(needle) != std::string::npos,
            path.string() + " is missing authority contract: " + needle);
}

void require_not_contains(const std::filesystem::path& path, const std::string& needle)
{
    require(read_file(path).find(needle) == std::string::npos,
            path.string() + " retains forbidden authority contract: " + needle);
}

void test_strict_bool_parsing()
{
    TempFixture fixture;
    const std::filesystem::path config_path = fixture.path() / "bool.par";
    {
        std::ofstream output(config_path);
        output << "truth = TrUe\nfalsehood = FALSE\n";
        output << "zero = 0\none = 1\non = on\noff = off\nyes = yes\nno = no\npartial = tru\n";
    }

    ConfigParser parser;
    require(parser.Load(config_path.string()), "strict-bool fixture did not load");
    require(parser.GetBool("truth", false), "mixed-case true was rejected");
    require(!parser.GetBool("falsehood", true), "mixed-case false was rejected");
    for (const char* key : {"zero", "one", "on", "off", "yes", "no", "partial"}) {
        bool rejected = false;
        try {
            static_cast<void>(parser.GetBool(key, false));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected, std::string("strict bool accepted forbidden token: ") + key);
    }
}

void test_case_api_and_log_directory_fallback()
{
    using RootWidth = double (*)(const SimConfig&, int);
    static_assert(std::is_same_v<decltype(&ProblemHelper::GetRootCellWidth), RootWidth>);
    TempFixture fixture;
    const auto fallback_path = fixture.path() / "fallback.par";
    std::ofstream(fallback_path) << "out_dir = authority-output\n";
    const SimConfig fallback = RuntimeParams::Load(fallback_path.string());
    require(fallback.Get<std::string>("log_dir", fallback.io.out_dir) == "authority-output",
            "log_dir did not default to out_dir");
    const auto override_path = fixture.path() / "override.par";
    std::ofstream(override_path) << "out_dir = authority-output\nlog_dir = authority-log\n";
    const SimConfig override = RuntimeParams::Load(override_path.string());
    require(override.Get<std::string>("log_dir", override.io.out_dir) == "authority-log",
            "explicit log_dir did not override out_dir");
}

void test_runtime_enum_token_canonicalization()
{
    using namespace arch::dispatch;

    TempFixture fixture;
    const SimConfig lowercase = load_fixture_config(
        fixture, "lowercase-enums.par",
        "geometry = cartesian\n"
        "nblockx1 = 1\n"
        "nblockx2 = 1\n"
        "nblockx3 = 1\n"
        "x1l_boundary_type = periodic\n"
        "x1r_boundary_type = outflow\n"
        "x2l_boundary_type = reflect\n"
        "x2r_boundary_type = reflecting\n"
        "x3l_boundary_type = periodic\n"
        "x3r_boundary_type = outflow\n"
        "gravity_type = external\n"
        "gravity_g_x = 1.25\n"
        "gravity_g_y = -2.5\n"
        "gravity_g_z = 3.75\n");
    const SimConfig mixed_case = load_fixture_config(
        fixture, "mixed-case-enums.par",
        "geometry = CaRtEsIaN\n"
        "nblockx1 = 1\n"
        "nblockx2 = 1\n"
        "nblockx3 = 1\n"
        "x1l_boundary_type = PeRiOdIc\n"
        "x1r_boundary_type = OuTfLoW\n"
        "x2l_boundary_type = ReFlEcT\n"
        "x2r_boundary_type = ReFlEcTiNg\n"
        "x3l_boundary_type = pErIoDiC\n"
        "x3r_boundary_type = oUtFlOw\n"
        "gravity_type = ExTeRnAl\n"
        "gravity_g_x = 1.25\n"
        "gravity_g_y = -2.5\n"
        "gravity_g_z = 3.75\n");

    require(mixed_case.grid.geometry == lowercase.grid.geometry,
            "mixed-case Cartesian was not stored canonically");
    require(mixed_case.grid.x1l_boundary_type == lowercase.grid.x1l_boundary_type
                && mixed_case.grid.x1r_boundary_type == lowercase.grid.x1r_boundary_type
                && mixed_case.grid.x2l_boundary_type == lowercase.grid.x2l_boundary_type
                && mixed_case.grid.x2r_boundary_type == lowercase.grid.x2r_boundary_type
                && mixed_case.grid.x3l_boundary_type == lowercase.grid.x3l_boundary_type
                && mixed_case.grid.x3r_boundary_type == lowercase.grid.x3r_boundary_type,
            "mixed-case boundary types were not stored canonically");
    require(mixed_case.physics.gravity.type == lowercase.physics.gravity.type
                && mixed_case.physics.gravity.g_x == lowercase.physics.gravity.g_x
                && mixed_case.physics.gravity.g_y == lowercase.physics.gravity.g_y
                && mixed_case.physics.gravity.g_z == lowercase.physics.gravity.g_z,
            "mixed-case External did not follow the lowercase load path");

    const auto lowercase_requirements =
        resolve_execution_requirements(lowercase, 0);
    const auto mixed_case_requirements =
        resolve_execution_requirements(mixed_case, 0);
    require(lowercase_requirements.ok && mixed_case_requirements.ok,
            "canonical enum tokens did not reach requirement resolution");
    require(same_requirements(
                lowercase_requirements.value, mixed_case_requirements.value),
            "mixed-case enum tokens selected a different execution path");
    require(mixed_case_requirements.value.geometry == GeometryId::Cartesian
                && mixed_case_requirements.value.gravity == GravityId::External
                && (mixed_case_requirements.value.boundary_features
                    & boundary_bit(BoundaryFeature::Periodic)) != 0,
            "mixed-case enum tokens selected the wrong resolved requirements");

    const SimConfig unknown = load_fixture_config(
        fixture, "unknown-enums.par",
        "geometry = NoSuchGeometry\n"
        "nblockx1 = 1\n"
        "nblockx2 = 1\n"
        "nblockx3 = 1\n"
        "x1l_boundary_type = NoSuchBoundary\n"
        "gravity_type = NoSuchGravity\n");
    require(unknown.grid.geometry == "nosuchgeometry"
                && unknown.grid.x1l_boundary_type == "nosuchboundary"
                && unknown.physics.gravity.type == "nosuchgravity",
            "unknown enum tokens were not preserved modulo ASCII case");
    require(!resolve_execution_requirements(unknown, 0).ok,
            "unknown geometry was silently mapped to a valid mode");
    SimConfig invalid = unknown;
    invalid.grid.geometry = lowercase.grid.geometry;
    require(!resolve_execution_requirements(invalid, 0).ok,
            "unknown gravity was silently mapped to a valid mode");
    invalid.physics.gravity.type = lowercase.physics.gravity.type;
    require(!resolve_execution_requirements(invalid, 0).ok,
            "unknown active boundary was silently mapped to a valid mode");

    const SimConfig inactive_faces = load_fixture_config(
        fixture, "inactive-face-enums.par",
        "geometry = Cartesian\n"
        "nblockx1 = 1\n"
        "nblockx2 = 0\n"
        "nblockx3 = 0\n"
        "x1l_boundary_type = Periodic\n"
        "x1r_boundary_type = Outflow\n"
        "x2l_boundary_type = IgnoredInactiveLower\n"
        "x2r_boundary_type = IgnoredInactiveUpper\n"
        "x3l_boundary_type = AlsoIgnoredInactiveLower\n"
        "x3r_boundary_type = AlsoIgnoredInactiveUpper\n"
        "gravity_type = External\n");
    const auto inactive_requirements =
        resolve_execution_requirements(inactive_faces, 0);
    require(inactive_requirements.ok,
            "enum canonicalization changed inactive-face semantics");
    require(inactive_requirements.value.boundary_features
                == (boundary_bit(BoundaryFeature::Periodic)
                    | boundary_bit(BoundaryFeature::Outflow)),
            "inactive faces contributed boundary requirements");
}

void test_portability_and_fixture_concurrency()
{
    require(ordinary_cxx_portability_witness(35) == 42,
            "ordinary C++ portability annotation witness failed");
    require(ordinary_cxx_forceinline_witness(21) == 42,
            "ordinary C++ force-inline compatibility witness failed");
    std::atomic<int> created{0};
    std::vector<std::thread> workers;
    for (int index = 0; index != 24; ++index) {
        workers.emplace_back([&created] {
            TempFixture fixture;
            const auto file = fixture.path() / "nonce.txt";
            std::ofstream(file) << "ok";
            if (std::filesystem::is_regular_file(file)) {
                created.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    require(created == 24, "concurrent authority fixture creation collided");
}

void test_conductivity_charge_factor()
{
    constexpr std::array<double, 2> xn{0.2, 0.8};
    constexpr std::array<double, 2> zion{1.0, 6.0};
    constexpr std::array<double, 2> aion_inverse{1.0, 1.0 / 12.0};
    const double conductivity = ConductivityMath::compute_stellar_conductivity(
        2.0e7, 1.0e6, 1.0e22, 4.0e29, 2.0,
        xn.data(), static_cast<int>(xn.size()), zion.data(), aion_inverse.data());

    // This state enters the electron-ion branch and changes decisively if
    // vie uses iec / zbar rather than the authoritative iec * zbar.
    constexpr double expected = 7.074738801307793e15;
    require(std::isfinite(conductivity), "conductivity is not finite");
    require(std::abs(conductivity - expected) <= std::abs(expected) * 2.0e-12,
            "conductivity no longer uses the authoritative charge factor");
}

void test_terminal_time_alignment()
{
    SimConfig config{};
    config.io.tmax = 1.0;
    RunState near_final{};
    near_final.time = 1.0 - 5.0e-13;
    SimulationController controller(config, near_final);
    require(controller.is_finished(), "relative terminal tolerance did not finish near final time");

    RunState residual{};
    residual.time = 1.0 - 5.0e-15;
    SimulationController residual_controller(config, residual);
    const double exact_residual = residual_controller.sync_dt(1.0);
    require(std::abs(exact_residual - 5.0e-15) <= 2.0e-16,
            "terminal timestep was replaced by an artificial floor");
    residual_controller.advance(exact_residual);
    require(residual_controller.t_current == config.io.tmax,
            "final timestep did not snap exactly to tmax");
}

struct CompositionSensitiveEos {
    void evaluate_state(eos_state_t& state) const
    {
        const double composition_scale = 1.0 + 0.4 * state.Xi[0] + 0.7 * state.Xi[1] * state.Xi[1];
        state.P = composition_scale * std::pow(state.rho, 1.15) * std::pow(state.T, 0.85);
        // The deliberately non-ideal pressure surface has a separately
        // prescribed isentropic derivative, as tabular EOS policies do.
        state.cv = 1.0;
        state.dp_dT = 0.35 * state.rho;
        state.sound_speed = std::sqrt(1.45 * state.P / state.rho);
    }
};

void test_shared_fixed_composition_isentrope()
{
    std::array<double, 2> mass_fractions{0.3, 0.7};
    const std::array<double, 2> original = mass_fractions;
    const auto state = eos_utils::get_isentropic_state_at_pressure_factor(
        CompositionSensitiveEos{}, 1.0, 1.0, mass_fractions.data(), 1.05);
    require(std::abs(state.pressure - 1.05 * 1.463) <= 1.05 * 1.463 * 3.0e-12,
            "shared isentrope did not reach the non-ideal EOS pressure target");
    require(state.rho != 1.0 && state.temperature != 1.0,
            "shared isentrope collapsed to a fixed ideal relation");
    require(mass_fractions == original,
            "shared fixed-composition isentrope modified mass fractions");
}

void test_canonical_helm_paths()
{
    const std::filesystem::path root = ARCH_SOURCE_DIR;
    require(std::filesystem::is_regular_file(root / kCanonicalHelmPath),
            "canonical Helm table is absent");
    require(!std::filesystem::exists(root / kRetiredHelmPath),
            "retired root Helm table reappeared");

}

} // namespace

int main()
{
    try {
        test_strict_bool_parsing();
        test_portability_and_fixture_concurrency();
        test_case_api_and_log_directory_fallback();
        test_runtime_enum_token_canonicalization();
        test_conductivity_charge_factor();
        test_terminal_time_alignment();
        test_shared_fixed_composition_isentrope();
        test_canonical_helm_paths();
        std::cout << "latest-main authority: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "latest-main authority: FAIL: " << error.what() << '\n';
        return 1;
    }
}
