#include "cuda/runtime/CudaBackend.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct TraceRow {
    std::uint64_t macro_step = 0;
    std::uint64_t operation = 0;
    std::uint64_t uid = 0;
    std::uint64_t epoch = 0;
    std::uint64_t storage = 0;
    std::uint64_t slot = 0;
    std::uint64_t interior_residency = 0;
    std::uint64_t interior_version = 0;
    std::uint64_t interior_pending = 0;
    std::uint64_t interior_token = 0;
    std::uint64_t interior_token_state = 0;
    std::uint64_t ghost_residency = 0;
    std::uint64_t ghost_version = 0;
    std::uint64_t ghost_source_version = 0;
    std::uint64_t ghost_pending = 0;
    std::uint64_t ghost_token = 0;
    std::uint64_t ghost_token_state = 0;
    std::uint64_t bytes_h2d = 0;
    std::uint64_t bytes_d2h = 0;
    std::uint64_t kernel_count = 0;
    std::uint64_t stream_sync_count = 0;
};

std::vector<TraceRow> read_trace(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open backend trace");
    std::string line;
    require(static_cast<bool>(std::getline(input, line)),
            "backend trace has no header");
    require(line ==
                "macro_step\toperation\tuid\tepoch\tstorage_generation\tslot"
                "\tinterior_residency\tinterior_version\tinterior_pending"
                "\tinterior_token\tinterior_token_state\tghost_residency"
                "\tghost_version\tghost_source_version\tghost_pending"
                "\tghost_token\tghost_token_state\tbytes_h2d\tbytes_d2h"
                "\tkernel_count\tstream_sync_count",
            "backend trace header drifted");
    std::vector<TraceRow> rows;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        std::replace(line.begin(), line.end(), '\t', ' ');
        std::istringstream values(line);
        TraceRow row{};
        values >> row.macro_step >> row.operation >> row.uid >> row.epoch
               >> row.storage >> row.slot >> row.interior_residency
               >> row.interior_version >> row.interior_pending
               >> row.interior_token >> row.interior_token_state
               >> row.ghost_residency >> row.ghost_version
               >> row.ghost_source_version >> row.ghost_pending
               >> row.ghost_token >> row.ghost_token_state
               >> row.bytes_h2d >> row.bytes_d2h >> row.kernel_count
               >> row.stream_sync_count;
        require(static_cast<bool>(values), "backend trace row is malformed");
        std::string extra;
        require(!(values >> extra), "backend trace row has extra fields");
        rows.push_back(row);
    }
    return rows;
}

std::map<std::string, std::string> read_plan(
    const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open backend plan");
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const auto split = line.find('=');
        require(split != std::string::npos && split != 0,
                "backend plan row is malformed");
        const bool inserted = values.emplace(
            line.substr(0, split), line.substr(split + 1)).second;
        require(inserted, "backend plan contains a duplicate key");
    }
    return values;
}

void validate_trace(const std::filesystem::path& path, int steps)
{
    require(steps == 1 || steps == 2 || steps == 5 || steps == 10
                || steps == 11,
            "unsupported single-level trace step count");
    const auto rows = read_trace(path);
    const int host_reads = (steps >= 10 ? 1 : 0) + (steps >= 11 ? 1 : 0);
    require(rows.size()
                == static_cast<std::size_t>(1 + 2 * steps + 2 * host_reads),
            "single-level trace operation count drifted");

    constexpr auto initial = static_cast<std::uint64_t>(
        arch::backend::BackendOperation::InitialUpload);
    constexpr auto materialize = static_cast<std::uint64_t>(
        arch::backend::BackendOperation::Materialize);
    constexpr auto boundary = static_cast<std::uint64_t>(
        arch::backend::BackendOperation::PhysicalBoundary);
    constexpr auto hydro = static_cast<std::uint64_t>(
        arch::backend::BackendOperation::HydroStage);
    constexpr auto complete = static_cast<std::uint64_t>(
        arch::state::CompletionState::Complete);
    constexpr auto no_pending = static_cast<std::uint64_t>(
        arch::state::PendingTransferPhase::None);
    constexpr auto invalid_residency = static_cast<std::uint64_t>(
        arch::state::StateResidency::Invalid);
    constexpr auto invalid_completion = static_cast<std::uint64_t>(
        arch::state::CompletionState::None);

    const auto require_settled = [&](const TraceRow& row) {
        require(row.interior_pending == no_pending
                    && row.ghost_pending == no_pending
                    && row.interior_token_state == complete
                    && row.ghost_token_state == complete,
                "completed operation retained a pending state");
        require(row.ghost_version == row.interior_version
                    && row.ghost_source_version == row.interior_version,
                "completed boundary does not match interior version");
    };

    TraceRow mismatched_source = rows.front();
    ++mismatched_source.ghost_source_version;
    bool mismatched_source_rejected = false;
    try {
        require_settled(mismatched_source);
    } catch (const std::runtime_error&) {
        mismatched_source_rejected = true;
    }
    require(mismatched_source_rejected,
            "mismatched ghost source version was accepted");

    require(rows.front().operation == initial
                && rows.front().macro_step == 0
                && rows.front().bytes_h2d > 0
                && rows.front().bytes_d2h == 0
                && rows.front().kernel_count == 0
                && rows.front().stream_sync_count == 1,
            "initial Current upload trace drifted");
    require_settled(rows.front());

    std::size_t cursor = 1;
    std::uint64_t previous_token = std::max(
        rows.front().interior_token, rows.front().ghost_token);
    std::uint64_t previous_version = rows.front().interior_version;
    const auto require_identity_and_clock = [&](const TraceRow& row) {
        require(row.uid != 0 && row.epoch != 0 && row.storage != 0
                    && row.slot == static_cast<std::uint64_t>(
                        arch::state::StateSlot::Current),
                "backend trace identity drifted");
        const std::uint64_t token = std::max(
            row.interior_token, row.ghost_token);
        require(token > previous_token,
                "backend completion tokens are not strictly monotonic");
        require(row.interior_version >= previous_version,
                "backend state version regressed");
        previous_token = token;
        previous_version = row.interior_version;
    };
    for (int step = 0; step < steps; ++step) {
        if (step == 5 || step == 10) {
            const TraceRow& refresh = rows[cursor++];
            const TraceRow& output = rows[cursor++];
            require(refresh.macro_step == static_cast<std::uint64_t>(step)
                        && refresh.operation == boundary
                        && refresh.kernel_count > 0
                        && refresh.stream_sync_count > 0,
                    "Host-read did not first refresh Device ghost");
            require(output.macro_step == static_cast<std::uint64_t>(step)
                        && output.operation == materialize
                        && output.bytes_h2d == 0 && output.bytes_d2h > 0
                        && output.kernel_count == 0
                        && output.stream_sync_count == 1,
                    "Host-read trace did not perform one bounded D2H");
            require_settled(refresh);
            require_settled(output);
            require_identity_and_clock(refresh);
            require_identity_and_clock(output);
        }
        const TraceRow& physical = rows[cursor++];
        const TraceRow& advance = rows[cursor++];
        require(physical.macro_step == static_cast<std::uint64_t>(step)
                    && physical.operation == boundary
                    && physical.bytes_h2d == 0 && physical.bytes_d2h == 0
                    && physical.kernel_count > 0
                    && physical.stream_sync_count > 0,
                "physical-boundary trace ordering drifted");
        require(advance.macro_step == static_cast<std::uint64_t>(step)
                    && advance.operation == hydro
                    && advance.bytes_h2d == 0 && advance.bytes_d2h == 0
                    && advance.kernel_count > 0
                    && advance.stream_sync_count > 0,
                "Hydro-stage trace ordering drifted");

        require_settled(physical);
        require(advance.interior_pending == no_pending
                    && advance.ghost_pending == no_pending
                    && advance.interior_token_state == complete
                    && advance.ghost_residency == invalid_residency
                    && advance.ghost_version == 0
                    && advance.ghost_source_version == 0
                    && advance.ghost_token == 0
                    && advance.ghost_token_state == invalid_completion,
                "Hydro publication did not invalidate Current ghost");
        for (const TraceRow* row : {&physical, &advance}) {
            require_identity_and_clock(*row);
        }
    }
    require(cursor == rows.size(), "backend trace has trailing operations");
}

void validate_burn_rkl_trace(const std::filesystem::path& trace_path,
                             const std::filesystem::path& plan_path,
                             const std::string& expected_diffusion,
                             int steps)
{
    require(expected_diffusion == "rkl1" || expected_diffusion == "rkl2",
            "unsupported Burn+RKL route");
    require(steps == 2, "unsupported Burn+RKL trace step count");
    const bool rkl2 = expected_diffusion == "rkl2";
    const auto plan = read_plan(plan_path);
    const auto require_plan = [&](const char* key, const char* expected) {
        const auto found = plan.find(key);
        require(found != plan.end() && found->second == expected,
                "resolved Burn+RKL plan drifted");
    };
    require_plan("requested", "cuda");
    require_plan("resolved", "cuda");
    require_plan("flux", "hllc");
    require_plan("reconstruction", "ppm");
    require_plan("limiter", "minmod");
    require_plan("time", "rk2");
    require_plan("eos", "helmholtz");
    require_plan("network", "aprox13");
    require_plan("ode", "bd");
    require_plan("linear", "denselu");
    require_plan("diffusion", expected_diffusion.c_str());

    constexpr auto initial = static_cast<std::uint64_t>(
        arch::backend::BackendOperation::InitialUpload);
    constexpr auto materialize = static_cast<std::uint64_t>(
        arch::backend::BackendOperation::Materialize);
    constexpr auto boundary = static_cast<std::uint64_t>(
        arch::backend::BackendOperation::PhysicalBoundary);
    constexpr auto hydro = static_cast<std::uint64_t>(
        arch::backend::BackendOperation::HydroStage);
    constexpr auto copy = static_cast<std::uint64_t>(
        arch::backend::BackendOperation::DiffusionCopy);
    constexpr auto diffusion = static_cast<std::uint64_t>(
        arch::backend::BackendOperation::DiffusionStage);
    constexpr auto burn = static_cast<std::uint64_t>(
        arch::backend::BackendOperation::Burn);
    constexpr auto complete = static_cast<std::uint64_t>(
        arch::state::CompletionState::Complete);
    constexpr auto no_pending = static_cast<std::uint64_t>(
        arch::state::PendingTransferPhase::None);
    constexpr auto current = static_cast<std::uint64_t>(
        arch::state::StateSlot::Current);
    constexpr auto next = static_cast<std::uint64_t>(
        arch::state::StateSlot::Next);
    constexpr auto scratch = static_cast<std::uint64_t>(
        arch::state::StateSlot::Scratch);

    const auto rows = read_trace(trace_path);
    require(rows.size() == 29, "Burn+RKL trace operation count drifted");
    const auto require_common = [&](const TraceRow& row, int macro_step,
                                    std::uint64_t operation,
                                    std::uint64_t slot) {
        require(row.macro_step == static_cast<std::uint64_t>(macro_step)
                    && row.operation == operation && row.slot == slot,
                "Burn+RKL operation ordering drifted");
        require(row.uid != 0 && row.epoch != 0 && row.storage != 0,
                "Burn+RKL identity drifted");
        require(row.interior_pending == no_pending
                    && row.ghost_pending == no_pending,
                "Burn+RKL operation retained a pending transfer");
    };
    const auto require_settled = [&](const TraceRow& row) {
        require(row.interior_token_state == complete
                    && row.ghost_token_state == complete
                    && row.ghost_version == row.interior_version
                    && row.ghost_source_version == row.interior_version,
                "Burn+RKL operation did not leave matching ghost state");
    };
    std::size_t boundary_validations = 0;
    const auto require_boundary = [&](const TraceRow& row, int macro_step) {
        require_common(row, macro_step, boundary, current);
        require(row.bytes_h2d == 0 && row.bytes_d2h == 0
                    && row.kernel_count == 1 && row.stream_sync_count == 1,
                "Burn+RKL physical-boundary counters drifted");
        require_settled(row);
        ++boundary_validations;
    };
    const auto require_copy = [&](const TraceRow& row, int macro_step,
                                  std::uint64_t destination) {
        require_common(row, macro_step, copy, destination);
        require(row.bytes_h2d == 0 && row.bytes_d2h == 0
                    && row.kernel_count == 0 && row.stream_sync_count == 1,
                "Burn+RKL slot-copy counters drifted");
        require_settled(row);
    };
    const auto require_diffusion = [&](const TraceRow& row, int macro_step) {
        require_common(row, macro_step, diffusion, current);
        require(row.bytes_h2d == 0
                    && row.bytes_d2h == static_cast<std::uint64_t>(rkl2 ? 8 : 4)
                    && row.kernel_count == static_cast<std::uint64_t>(rkl2 ? 8 : 4)
                    && row.stream_sync_count == static_cast<std::uint64_t>(rkl2 ? 4 : 2),
                "resolved RKL stage counters drifted");
        require_settled(row);
    };
    const auto require_burn = [&](const TraceRow& row, int macro_step) {
        require_common(row, macro_step, burn, current);
        require(row.bytes_h2d == 0 && row.bytes_d2h == 24
                    && row.kernel_count == 2 && row.stream_sync_count == 1,
                "Burn route counters drifted");
        require_settled(row);
    };

    require_common(rows[0], 0, initial, current);
    require(rows[0].bytes_h2d == 3648 && rows[0].bytes_d2h == 0
                && rows[0].kernel_count == 0 && rows[0].stream_sync_count == 1,
            "Burn+RKL initial upload drifted");
    require_settled(rows[0]);

    std::size_t cursor = 1;
    std::uint64_t last_version = rows[0].interior_version;
    for (int step = 0; step < steps; ++step) {
        if (step != 0) {
            require_boundary(rows[cursor++], step);
            const auto& output = rows[cursor++];
            require_common(output, step, materialize, current);
            require(output.bytes_h2d == 0 && output.bytes_d2h == 3648
                        && output.kernel_count == 0
                        && output.stream_sync_count == 1,
                    "Burn+RKL output materialization drifted");
            require_settled(output);
        }
        require_burn(rows[cursor++], step);
        require_boundary(rows[cursor++], step);
        require_copy(rows[cursor++], step, scratch);
        require_copy(rows[cursor++], step, next);
        require_diffusion(rows[cursor++], step);
        require_boundary(rows[cursor++], step);

        const auto& hydro_row = rows[cursor++];
        require_common(hydro_row, step, hydro, current);
        require(hydro_row.bytes_h2d == 0 && hydro_row.bytes_d2h == 0
                    && hydro_row.kernel_count == 7
                    && hydro_row.stream_sync_count == 3,
                "Burn+RKL Hydro counters drifted");
        require(hydro_row.interior_token_state == complete
                    && hydro_row.ghost_version == 0
                    && hydro_row.ghost_source_version == 0,
                "Burn+RKL Hydro publication did not invalidate ghost state");

        require_boundary(rows[cursor++], step);
        require_copy(rows[cursor++], step, scratch);
        require_copy(rows[cursor++], step, next);
        require_diffusion(rows[cursor++], step);
        require_burn(rows[cursor++], step);
        require(rows[cursor - 1].interior_version > last_version,
                "Burn+RKL macro step did not advance state version");
        last_version = rows[cursor - 1].interior_version;
    }
    require_boundary(rows[cursor++], steps);
    const auto& final_output = rows[cursor++];
    require_common(final_output, steps, materialize, current);
    require(final_output.bytes_h2d == 0 && final_output.bytes_d2h == 3648
                && final_output.kernel_count == 0
                && final_output.stream_sync_count == 1,
            "Burn+RKL final materialization drifted");
    require_settled(final_output);
    require(boundary_validations == static_cast<std::size_t>(4 * steps),
            "Burn+RKL boundary validation count drifted");
    require(cursor == rows.size(), "Burn+RKL trace has trailing operations");
}

} // namespace

int main(int argc, char** argv)
{
    static_assert(std::is_base_of_v<arch::backend::ComputeBackend,
                                    arch::cuda::CudaBackend>);
    static_assert(!std::is_copy_constructible_v<arch::cuda::CudaBackend>);
    static_assert(!std::is_move_constructible_v<arch::cuda::CudaBackend>);
    if (argc == 1) return 0;
    if (argc == 3) {
        validate_trace(argv[1], std::stoi(argv[2]));
        return 0;
    }
    if (argc == 5) {
        validate_burn_rkl_trace(argv[1], argv[2], argv[3],
                                std::stoi(argv[4]));
        return 0;
    }
    throw std::invalid_argument(
        "usage: arch_cuda_single_level_validation TRACE STEPS | "
        "TRACE PLAN {rkl1|rkl2} STEPS");
}
