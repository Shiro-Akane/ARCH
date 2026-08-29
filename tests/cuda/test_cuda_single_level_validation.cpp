#include "cuda/runtime/CudaBackend.h"
#include "io/hdf5/HDF5Writer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
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

bool within_checkpoint_time_budget(double reference, double candidate,
                                   int accepted_steps)
{
    if (!std::isfinite(reference) || !std::isfinite(candidate)) return false;
    if (reference == candidate) return true;
    const int budget = 2 * std::clamp(accepted_steps, 1, 10);
    double adjacent = reference;
    for (int ulp = 0; ulp < budget; ++ulp) {
        adjacent = std::nextafter(adjacent, candidate);
        if (adjacent == candidate) return true;
    }
    return false;
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

void compare_real_checkpoints(const std::filesystem::path& reference_path,
                              const std::filesystem::path& candidate_path,
                              double rtol, double atol)
{
    require(std::isfinite(rtol) && rtol >= 0.0
                && std::isfinite(atol) && atol >= 0.0,
            "checkpoint comparison tolerance is invalid");
    const io::CheckpointData reference =
        io::read_hdf5_chk_impl(reference_path.string());
    const io::CheckpointData candidate =
        io::read_hdf5_chk_impl(candidate_path.string());
    require(reference.step_count == candidate.step_count,
            "checkpoint accepted-step mismatch");
    require(within_checkpoint_time_budget(
                reference.time, candidate.time, reference.step_count),
            "checkpoint physical-time mismatch");
    require(reference.dim == candidate.dim
                && reference.geometry == candidate.geometry
                && reference.num_species == candidate.num_species
                && reference.cells_per_block == candidate.cells_per_block
                && reference.levels == candidate.levels
                && reference.logical_x1 == candidate.logical_x1
                && reference.logical_x2 == candidate.logical_x2
                && reference.logical_x3 == candidate.logical_x3,
            "checkpoint topology or layout mismatch");

    struct FieldView {
        const char* name;
        const std::vector<double>* reference;
        const std::vector<double>* candidate;
    };
    const std::array fields{
        FieldView{"rho", &reference.rho, &candidate.rho},
        FieldView{"mom_u", &reference.mom_u, &candidate.mom_u},
        FieldView{"mom_v", &reference.mom_v, &candidate.mom_v},
        FieldView{"mom_w", &reference.mom_w, &candidate.mom_w},
        FieldView{"eng", &reference.eng, &candidate.eng},
        FieldView{"rhoX", &reference.rhoX, &candidate.rhoX}};
    double global_max_abs = 0.0;
    double global_max_rel = 0.0;
    for (const auto& field : fields) {
        require(field.reference->size() == field.candidate->size(),
                "checkpoint field shape mismatch");
        for (std::size_t index = 0; index < field.reference->size(); ++index) {
            const double left = (*field.reference)[index];
            const double right = (*field.candidate)[index];
            require(std::isfinite(left) && std::isfinite(right),
                    "checkpoint field contains a non-finite value");
            const double absolute = std::abs(left - right);
            const double scale = std::max(std::abs(left), std::abs(right));
            const double relative = scale == 0.0 ? 0.0 : absolute / scale;
            global_max_abs = std::max(global_max_abs, absolute);
            global_max_rel = std::max(global_max_rel, relative);
            if (absolute > atol + rtol * std::abs(left)) {
                std::ostringstream message;
                message << "checkpoint first mismatch field=" << field.name
                        << " index=" << index << " reference=" << left
                        << " candidate=" << right << " abs=" << absolute
                        << " rel=" << relative;
                throw std::runtime_error(message.str());
            }
        }
    }
    std::cout << "{\"status\":\"pass\",\"step\":"
              << reference.step_count << ",\"time\":" << reference.time
              << ",\"max_abs\":" << global_max_abs
              << ",\"max_rel\":" << global_max_rel << "}\n";
}

struct SodPrimitive {
    double rho;
    double velocity;
    double pressure;
};

class SodExactSolution
{
    static constexpr double gamma_ = 1.4;
    static constexpr double interface_ = 0.5;
    static constexpr SodPrimitive left_{1.0, 0.0, 1.0};
    static constexpr SodPrimitive right_{0.125, 0.0, 0.1};
    double pressure_star_ = 0.0;
    double velocity_star_ = 0.0;

    static std::pair<double, double> pressure_function(
        double pressure, const SodPrimitive& state)
    {
        const double sound = std::sqrt(gamma_ * state.pressure / state.rho);
        if (pressure > state.pressure) {
            const double a = 2.0 / ((gamma_ + 1.0) * state.rho);
            const double b = (gamma_ - 1.0) / (gamma_ + 1.0)
                * state.pressure;
            const double root = std::sqrt(a / (pressure + b));
            return {(pressure - state.pressure) * root,
                    root * (1.0 - 0.5 * (pressure - state.pressure)
                                     / (pressure + b))};
        }
        const double ratio = pressure / state.pressure;
        const double exponent = (gamma_ - 1.0) / (2.0 * gamma_);
        return {2.0 * sound / (gamma_ - 1.0)
                    * (std::pow(ratio, exponent) - 1.0),
                std::pow(ratio, -(gamma_ + 1.0) / (2.0 * gamma_))
                    / (state.rho * sound)};
    }

    static double star_density(
        const SodPrimitive& state, double pressure_star)
    {
        const double ratio = pressure_star / state.pressure;
        if (pressure_star > state.pressure) {
            const double q = (gamma_ - 1.0) / (gamma_ + 1.0);
            return state.rho * (ratio + q) / (q * ratio + 1.0);
        }
        return state.rho * std::pow(ratio, 1.0 / gamma_);
    }

public:
    SodExactSolution()
    {
        const double left_sound =
            std::sqrt(gamma_ * left_.pressure / left_.rho);
        const double right_sound =
            std::sqrt(gamma_ * right_.pressure / right_.rho);
        pressure_star_ = std::max(
            1.0e-12,
            0.5 * (left_.pressure + right_.pressure)
                - 0.125 * (right_.velocity - left_.velocity)
                    * (left_.rho + right_.rho)
                    * (left_sound + right_sound));
        for (int iteration = 0; iteration < 64; ++iteration) {
            const auto [left_value, left_derivative] =
                pressure_function(pressure_star_, left_);
            const auto [right_value, right_derivative] =
                pressure_function(pressure_star_, right_);
            const double next = std::max(
                1.0e-12,
                pressure_star_
                    - (left_value + right_value
                       + right_.velocity - left_.velocity)
                        / (left_derivative + right_derivative));
            if (std::abs(next - pressure_star_)
                    <= 1.0e-14 * (next + pressure_star_)) {
                pressure_star_ = next;
                break;
            }
            pressure_star_ = next;
        }
        const auto [left_value, unused_left] =
            pressure_function(pressure_star_, left_);
        const auto [right_value, unused_right] =
            pressure_function(pressure_star_, right_);
        (void)unused_left;
        (void)unused_right;
        velocity_star_ = 0.5 * (left_.velocity + right_.velocity
                                + right_value - left_value);
    }

    SodPrimitive sample(double x, double time) const
    {
        if (time <= 0.0)
            return x < interface_ ? left_ : right_;
        const double similarity = (x - interface_) / time;
        if (similarity <= velocity_star_) {
            const double sound =
                std::sqrt(gamma_ * left_.pressure / left_.rho);
            if (pressure_star_ > left_.pressure) {
                const double speed = left_.velocity - sound * std::sqrt(
                    (gamma_ + 1.0) / (2.0 * gamma_)
                        * pressure_star_ / left_.pressure
                    + (gamma_ - 1.0) / (2.0 * gamma_));
                return similarity <= speed
                    ? left_
                    : SodPrimitive{star_density(left_, pressure_star_),
                                   velocity_star_, pressure_star_};
            }
            const double head = left_.velocity - sound;
            const double star_sound = sound * std::pow(
                pressure_star_ / left_.pressure,
                (gamma_ - 1.0) / (2.0 * gamma_));
            const double tail = velocity_star_ - star_sound;
            if (similarity <= head) return left_;
            if (similarity >= tail)
                return {star_density(left_, pressure_star_),
                        velocity_star_, pressure_star_};
            const double velocity = 2.0 / (gamma_ + 1.0)
                * (sound + 0.5 * (gamma_ - 1.0) * left_.velocity
                   + similarity);
            const double local_sound = 2.0 / (gamma_ + 1.0)
                * (sound + 0.5 * (gamma_ - 1.0)
                               * (left_.velocity - similarity));
            const double ratio = local_sound / sound;
            return {left_.rho * std::pow(ratio, 2.0 / (gamma_ - 1.0)),
                    velocity,
                    left_.pressure
                        * std::pow(ratio, 2.0 * gamma_ / (gamma_ - 1.0))};
        }
        const double sound = std::sqrt(gamma_ * right_.pressure / right_.rho);
        if (pressure_star_ > right_.pressure) {
            const double speed = right_.velocity + sound * std::sqrt(
                (gamma_ + 1.0) / (2.0 * gamma_)
                    * pressure_star_ / right_.pressure
                + (gamma_ - 1.0) / (2.0 * gamma_));
            return similarity >= speed
                ? right_
                : SodPrimitive{star_density(right_, pressure_star_),
                               velocity_star_, pressure_star_};
        }
        const double head = right_.velocity + sound;
        const double star_sound = sound * std::pow(
            pressure_star_ / right_.pressure,
            (gamma_ - 1.0) / (2.0 * gamma_));
        const double tail = velocity_star_ + star_sound;
        if (similarity >= head) return right_;
        if (similarity <= tail)
            return {star_density(right_, pressure_star_),
                    velocity_star_, pressure_star_};
        const double velocity = 2.0 / (gamma_ + 1.0)
            * (-sound + 0.5 * (gamma_ - 1.0) * right_.velocity
               + similarity);
        const double local_sound = 2.0 / (gamma_ + 1.0)
            * (sound - 0.5 * (gamma_ - 1.0)
                           * (right_.velocity - similarity));
        const double ratio = local_sound / sound;
        return {right_.rho * std::pow(ratio, 2.0 / (gamma_ - 1.0)),
                velocity,
                right_.pressure
                    * std::pow(ratio, 2.0 * gamma_ / (gamma_ - 1.0))};
    }

    double right_shock_position(double time) const
    {
        const double sound = std::sqrt(gamma_ * right_.pressure / right_.rho);
        const double speed = right_.velocity + sound * std::sqrt(
            (gamma_ + 1.0) / (2.0 * gamma_)
                * pressure_star_ / right_.pressure
            + (gamma_ - 1.0) / (2.0 * gamma_));
        return interface_ + speed * time;
    }

    std::vector<double> wave_positions(double time) const
    {
        std::vector<double> positions{interface_ + velocity_star_ * time};
        const double left_sound =
            std::sqrt(gamma_ * left_.pressure / left_.rho);
        if (pressure_star_ > left_.pressure) {
            positions.push_back(interface_ + time * (left_.velocity
                - left_sound * std::sqrt(
                    (gamma_ + 1.0) / (2.0 * gamma_)
                        * pressure_star_ / left_.pressure
                    + (gamma_ - 1.0) / (2.0 * gamma_))));
        } else {
            positions.push_back(interface_ + time * (left_.velocity - left_sound));
            positions.push_back(interface_ + time * (velocity_star_
                - left_sound * std::pow(
                    pressure_star_ / left_.pressure,
                    (gamma_ - 1.0) / (2.0 * gamma_))));
        }
        const double right_sound =
            std::sqrt(gamma_ * right_.pressure / right_.rho);
        if (pressure_star_ > right_.pressure) {
            positions.push_back(right_shock_position(time));
        } else {
            positions.push_back(interface_ + time * (right_.velocity + right_sound));
            positions.push_back(interface_ + time * (velocity_star_
                + right_sound * std::pow(
                    pressure_star_ / right_.pressure,
                    (gamma_ - 1.0) / (2.0 * gamma_))));
        }
        return positions;
    }
};

std::array<double, 4> sod_cell_average(
    const SodExactSolution& exact, double lower, double upper, double time)
{
    static constexpr std::array<double, 8> nodes{
        -0.9602898564975363, -0.7966664774136267,
        -0.5255324099163290, -0.1834346424956498,
         0.1834346424956498,  0.5255324099163290,
         0.7966664774136267,  0.9602898564975363};
    static constexpr std::array<double, 8> weights{
        0.1012285362903763, 0.2223810344533745,
        0.3137066458778873, 0.3626837833783620,
        0.3626837833783620, 0.3137066458778873,
        0.2223810344533745, 0.1012285362903763};
    std::vector<double> cuts{lower, upper};
    for (double position : exact.wave_positions(time)) {
        if (position > lower && position < upper) cuts.push_back(position);
    }
    std::sort(cuts.begin(), cuts.end());
    std::array<double, 4> integral{};
    for (std::size_t segment = 0; segment + 1 < cuts.size(); ++segment) {
        const double midpoint = 0.5 * (cuts[segment] + cuts[segment + 1]);
        const double half_width = 0.5 * (cuts[segment + 1] - cuts[segment]);
        for (std::size_t point = 0; point < nodes.size(); ++point) {
            const SodPrimitive state = exact.sample(
                midpoint + half_width * nodes[point], time);
            const double energy = state.pressure / 0.4
                + 0.5 * state.rho * state.velocity * state.velocity;
            const std::array values{
                state.rho, state.velocity, state.pressure, energy};
            for (std::size_t field = 0; field < integral.size(); ++field)
                integral[field] += half_width * weights[point] * values[field];
        }
    }
    for (double& value : integral) value /= upper - lower;
    return integral;
}

void qualify_sod_checkpoint(const io::CheckpointData& checkpoint)
{
    const std::size_t blocks = checkpoint.levels.size();
    const std::size_t cells = blocks * checkpoint.cells_per_block;
    std::array<double, 4> l1{};
    std::array<double, 4> l2{};
    std::vector<double> pressure(cells);
    double minimum_rho = std::numeric_limits<double>::infinity();
    double minimum_eng = std::numeric_limits<double>::infinity();
    const SodExactSolution exact;
    for (std::size_t block = 0; block < blocks; ++block) {
        require(checkpoint.logical_x1[block] >= 0,
                "Sod logical block coordinate is negative");
        for (std::size_t local = 0; local < checkpoint.cells_per_block; ++local) {
            const std::size_t offset = block * checkpoint.cells_per_block + local;
            const std::size_t logical =
                static_cast<std::size_t>(checkpoint.logical_x1[block])
                    * checkpoint.cells_per_block + local;
            require(logical < cells, "Sod logical cell is outside the uniform root");
            const double rho = checkpoint.rho[offset];
            const double velocity = checkpoint.mom_u[offset] / rho;
            const double kinetic = 0.5 * (
                checkpoint.mom_u[offset] * checkpoint.mom_u[offset]
                + checkpoint.mom_v[offset] * checkpoint.mom_v[offset]
                + checkpoint.mom_w[offset] * checkpoint.mom_w[offset]) / rho;
            const double local_pressure = 0.4 * (checkpoint.eng[offset] - kinetic);
            const std::array observed{
                rho, velocity, local_pressure, checkpoint.eng[offset]};
            const double lower = static_cast<double>(logical) / cells;
            const auto expected = sod_cell_average(
                exact, lower, lower + 1.0 / cells, checkpoint.time);
            for (std::size_t field = 0; field < observed.size(); ++field) {
                const double error = std::abs(observed[field] - expected[field]);
                l1[field] += error;
                l2[field] += error * error;
            }
            pressure[logical] = local_pressure;
            minimum_rho = std::min(minimum_rho, rho);
            minimum_eng = std::min(minimum_eng, checkpoint.eng[offset]);
        }
    }
    for (std::size_t field = 0; field < l1.size(); ++field) {
        l1[field] /= static_cast<double>(cells);
        l2[field] = std::sqrt(l2[field] / static_cast<double>(cells));
    }
    std::size_t shock_interface = 1;
    double shock_jump = -1.0;
    for (std::size_t index = 0; index + 1 < pressure.size(); ++index) {
        const double jump = std::abs(pressure[index + 1] - pressure[index]);
        if (jump > shock_jump) {
            shock_jump = jump;
            shock_interface = index + 1;
        }
    }
    const double observed_shock = static_cast<double>(shock_interface) / cells;
    const double shock_error_cells = std::abs(
        observed_shock - exact.right_shock_position(checkpoint.time)) * cells;
    require(std::isfinite(minimum_rho) && minimum_rho > 0.0
                && std::isfinite(minimum_eng) && minimum_eng > 0.0,
            "Sod positivity failed");
    std::cout << "{\"status\":\"pass\",\"mode\":\"sod\",\"step\":"
              << checkpoint.step_count << ",\"time\":" << checkpoint.time
              << ",\"l1\":" << l1[0] << ",\"l2\":" << l2[0]
              << ",\"rho_l1\":" << l1[0] << ",\"rho_l2\":" << l2[0]
              << ",\"velocity_l1\":" << l1[1]
              << ",\"velocity_l2\":" << l2[1]
              << ",\"pressure_l1\":" << l1[2]
              << ",\"pressure_l2\":" << l2[2]
              << ",\"energy_l1\":" << l1[3]
              << ",\"energy_l2\":" << l2[3]
              << ",\"shock_position\":" << observed_shock
              << ",\"shock_position_error_cells\":" << shock_error_cells
              << ",\"min_rho\":" << minimum_rho
              << ",\"min_eng\":" << minimum_eng
              << ",\"species_sum_error\":0}\n";
}

void qualify_real_checkpoint(const std::filesystem::path& checkpoint_path,
                             const std::string& mode)
{
    const io::CheckpointData checkpoint =
        io::read_hdf5_chk_impl(checkpoint_path.string());
    require(checkpoint.dim == 1 && checkpoint.geometry == "cartesian"
                && checkpoint.cells_per_block > 0
                && checkpoint.levels.size() == checkpoint.logical_x1.size()
                && !checkpoint.levels.empty(),
            "qualification checkpoint topology is unsupported");
    require(std::all_of(checkpoint.levels.begin(), checkpoint.levels.end(),
                        [](int level) { return level == 0; }),
            "uniform qualification received an AMR checkpoint");
    const std::size_t blocks = checkpoint.levels.size();
    const std::size_t cells = blocks * checkpoint.cells_per_block;
    require(checkpoint.rho.size() == cells && checkpoint.mom_u.size() == cells
                && checkpoint.mom_v.size() == cells
                && checkpoint.mom_w.size() == cells
                && checkpoint.eng.size() == cells,
            "qualification checkpoint field shape drifted");
    if (mode == "sod") {
        qualify_sod_checkpoint(checkpoint);
        return;
    }
    constexpr double pi = 3.141592653589793238462643383279502884;
    const double half_phase = pi / static_cast<double>(cells);
    const double cell_average_factor = std::sin(half_phase) / half_phase;
    double l1 = 0.0;
    double l2 = 0.0;
    double linf = 0.0;
    double mean = 0.0;
    double mass = 0.0;
    double mom_u = 0.0;
    double mom_v = 0.0;
    double mom_w = 0.0;
    double eng_total = 0.0;
    double minimum_rho = std::numeric_limits<double>::infinity();
    double minimum_eng = std::numeric_limits<double>::infinity();
    double minimum_fraction = std::numeric_limits<double>::infinity();
    double maximum_fraction = -std::numeric_limits<double>::infinity();
    double species_sum_error = 0.0;
    for (std::size_t block = 0; block < blocks; ++block) {
        require(checkpoint.logical_x1[block] >= 0,
                "qualification logical block coordinate is negative");
        for (std::size_t local = 0; local < checkpoint.cells_per_block; ++local) {
            const std::size_t offset = block * checkpoint.cells_per_block + local;
            const std::size_t logical =
                static_cast<std::size_t>(checkpoint.logical_x1[block])
                    * checkpoint.cells_per_block + local;
            require(logical < cells,
                    "qualification logical cell is outside the uniform root");
            const double x = (static_cast<double>(logical) + 0.5)
                / static_cast<double>(cells);
            minimum_rho = std::min(minimum_rho, checkpoint.rho[offset]);
            minimum_eng = std::min(minimum_eng, checkpoint.eng[offset]);
            mass += checkpoint.rho[offset];
            mom_u += checkpoint.mom_u[offset];
            mom_v += checkpoint.mom_v[offset];
            mom_w += checkpoint.mom_w[offset];
            eng_total += checkpoint.eng[offset];
            double observed = checkpoint.rho[offset];
            double expected = observed;
            if (mode == "smooth") {
                expected = 1.0 + 0.2 * cell_average_factor
                    * std::sin(2.0 * pi * (x - checkpoint.time));
            } else if (mode == "diffusion") {
                require(checkpoint.num_species == 2
                            && checkpoint.rhoX.size() == 2 * cells,
                        "diffusion qualification species layout drifted");
                observed = checkpoint.rhoX[cells + offset]
                    / checkpoint.rho[offset];
                minimum_fraction = std::min(minimum_fraction, observed);
                maximum_fraction = std::max(maximum_fraction, observed);
                expected = 0.5 + 0.25 * cell_average_factor
                    * std::exp(-0.01 * 4.0 * pi * pi * checkpoint.time)
                    * std::cos(2.0 * pi * x);
            } else if (mode != "burn") {
                throw std::invalid_argument("unknown qualification mode");
            }
            if (mode != "burn") {
                const double error = std::abs(observed - expected);
                l1 += error;
                l2 += error * error;
                linf = std::max(linf, error);
                mean += observed;
            }
            if (checkpoint.num_species > 0) {
                double sum = 0.0;
                for (int species = 0; species < checkpoint.num_species; ++species)
                    sum += checkpoint.rhoX[
                        static_cast<std::size_t>(species) * cells + offset]
                        / checkpoint.rho[offset];
                species_sum_error = std::max(
                    species_sum_error, std::abs(sum - 1.0));
            }
        }
    }
    if (mode != "burn") {
        l1 /= static_cast<double>(cells);
        l2 = std::sqrt(l2 / static_cast<double>(cells));
        mean /= static_cast<double>(cells);
    }
    mass /= static_cast<double>(cells);
    mom_u /= static_cast<double>(cells);
    mom_v /= static_cast<double>(cells);
    mom_w /= static_cast<double>(cells);
    eng_total /= static_cast<double>(cells);
    require(std::isfinite(minimum_rho) && std::isfinite(minimum_eng)
                && minimum_rho > 0.0 && minimum_eng > 0.0,
            "qualification positivity failed");
    std::cout << "{\"status\":\"pass\",\"mode\":\"" << mode
              << "\",\"step\":" << checkpoint.step_count
              << ",\"time\":" << checkpoint.time
              << ",\"l1\":" << l1 << ",\"l2\":" << l2
              << ",\"linf\":" << linf << ",\"mean\":" << mean
              << ",\"mass\":" << mass
              << ",\"mom_u\":" << mom_u
              << ",\"mom_v\":" << mom_v
              << ",\"mom_w\":" << mom_w
              << ",\"eng_total\":" << eng_total
              << ",\"min_rho\":" << minimum_rho
              << ",\"min_eng\":" << minimum_eng
              << ",\"min_fraction\":"
              << (mode == "diffusion" ? minimum_fraction : 0.0)
              << ",\"max_fraction\":"
              << (mode == "diffusion" ? maximum_fraction : 0.0)
              << ",\"species_sum_error\":" << species_sum_error
              << "}\n";
}

} // namespace

int main(int argc, char** argv)
{
    static_assert(std::is_base_of_v<arch::backend::ComputeBackend,
                                    arch::cuda::CudaBackend>);
    static_assert(!std::is_copy_constructible_v<arch::cuda::CudaBackend>);
    static_assert(!std::is_move_constructible_v<arch::cuda::CudaBackend>);
    if (argc == 1) return 0;
    if (argc == 6 && std::string(argv[1]) == "--compare") {
        compare_real_checkpoints(
            argv[2], argv[3], std::stod(argv[4]), std::stod(argv[5]));
        return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "--qualify") {
        qualify_real_checkpoint(argv[2], argv[3]);
        return 0;
    }
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
        "usage: arch_cuda_single_level_validation --compare "
        "CPU_CHECKPOINT CUDA_CHECKPOINT "
        "RTOL ATOL | --qualify CHECKPOINT {smooth|diffusion|burn} | "
        "TRACE STEPS | "
        "TRACE PLAN {rkl1|rkl2} STEPS");
}
