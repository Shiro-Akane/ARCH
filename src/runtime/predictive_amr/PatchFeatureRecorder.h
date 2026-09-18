/**
 * @file PatchFeatureRecorder.h
 * @brief Read-only exporter of block-level features and canonical AMR decisions.
 *
 * The canonical AmrTree remains the sole owner of indicator evaluation,
 * balance closure, and topology changes. This observer only records the state
 * after RippleCheck and immediately before Regrid applies the decisions.
 * The driver materializes accepted state before calling the observer. Enabling
 * recording does not select a refinement policy or perform model inference.
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../amr/AmrTree.h"
#include "../../amr/MemoryPool.h"
#include "../../data/GlobalDefs.h"
#include "../../grid/GridMetrics.h"
#include "../../physics/diagnostics/VelocityDiagnostics.h"

namespace arch::runtime::predictive_amr {

/** Online mean and population variance: M2 += delta * (x - updated_mean). */
class RunningStats {
private:
    double minimum_ = std::numeric_limits<double>::infinity();
    double maximum_ = -std::numeric_limits<double>::infinity();
    double mean_ = 0.0;
    double m2_ = 0.0;
    std::size_t count_ = 0;

public:
    void Add(double value)
    {
        if (!std::isfinite(value))
            throw std::runtime_error("Predictive AMR recorder encountered a non-finite feature");
        minimum_ = std::min(minimum_, value);
        maximum_ = std::max(maximum_, value);
        ++count_;
        const double delta = value - mean_;
        mean_ += delta / static_cast<double>(count_);
        m2_ += delta * (value - mean_);
    }

    double Minimum() const { return count_ == 0 ? 0.0 : minimum_; }
    double Maximum() const { return count_ == 0 ? 0.0 : maximum_; }
    double Mean() const { return count_ == 0 ? 0.0 : mean_; }
    double StandardDeviation() const
    {
        return count_ == 0 ? 0.0 : std::sqrt(std::max(0.0, m2_ / static_cast<double>(count_)));
    }
    std::size_t Count() const { return count_; }
};

template <typename EosPolicy>
class PatchFeatureRecorder {
private:
    static constexpr int kSchemaVersion = 2;
    using Clock = std::chrono::steady_clock;

    bool enabled_ = false;
    std::shared_ptr<amr::MemoryPool> pool_;
    const EosPolicy* eos_ = nullptr;
    std::ofstream nodes_;
    std::ofstream edges_;
    std::ofstream events_;
    std::ofstream conservation_;
    std::uint64_t event_index_ = 0;
    std::uint64_t node_rows_ = 0;
    std::uint64_t edge_rows_ = 0;
    std::uint64_t conservation_snapshots_ = 0;
    double observer_ms_ = 0.0;
    double small_density_ = 0.0;
    double refine_threshold_ = 0.0;
    double derefine_threshold_ = 0.0;
    std::string prefix_;

    struct ConservedTotals {
        long double mass = 0.0L;
        long double momentum_x1 = 0.0L;
        long double momentum_x2 = 0.0L;
        long double momentum_x3 = 0.0L;
        long double energy = 0.0L;
        std::vector<long double> species_mass;
    };

    static void AccumulateConservedCell(ConservedTotals& totals,
                                        const FluidState& state,
                                        const Grid& grid,
                                        int i, int j, int k)
    {
        const int index = grid.GetIndex(i, j, k);
        const long double volume = static_cast<long double>(
            GridMetrics::CellVolume(grid, i, j, k));
        totals.mass += static_cast<long double>(state.rho[index]) * volume;
        totals.momentum_x1 += static_cast<long double>(state.mom_u[index]) * volume;
        totals.momentum_x2 += static_cast<long double>(state.mom_v[index]) * volume;
        totals.momentum_x3 += static_cast<long double>(state.mom_w[index]) * volume;
        totals.energy += static_cast<long double>(state.eng[index]) * volume;
        for (std::size_t species = 0; species < totals.species_mass.size(); ++species) {
            totals.species_mass[species] +=
                static_cast<long double>(state.rho[index]) *
                static_cast<long double>(state.X(static_cast<int>(species), index)) * volume;
        }
    }

    ConservedTotals ComputeConservedTotals(const amr::AmrTree& tree) const
    {
        ConservedTotals totals;
        const auto& active_blocks = tree.GetActiveBlocks();
        if (active_blocks.empty()) return totals;
        const int species_count =
            pool_->GetBlock(active_blocks.front()).fluid_state.GetNumSpecies();
        totals.species_mass.assign(static_cast<std::size_t>(species_count), 0.0L);
        for (const int block_id : active_blocks) {
            const amr::Block& block = pool_->GetBlock(block_id);
            if (block.fluid_state.GetNumSpecies() != species_count)
                throw std::runtime_error("Predictive AMR conservation audit found inconsistent species counts");
            for (int k = block.grid.Ks(); k < block.grid.Ke(); ++k)
                for (int j = block.grid.Js(); j < block.grid.Je(); ++j)
                    for (int i = block.grid.Is(); i < block.grid.Ie(); ++i)
                        AccumulateConservedCell(totals, block.fluid_state, block.grid, i, j, k);
        }
        return totals;
    }

    static double ElapsedMs(Clock::time_point begin, Clock::time_point end)
    {
        return std::chrono::duration<double, std::milli>(end - begin).count();
    }

    static void Open(std::ofstream& stream, const std::filesystem::path& path,
                     const char* header)
    {
        const std::filesystem::path parent = path.parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        stream.open(path, std::ios::out | std::ios::trunc);
        if (!stream)
            throw std::runtime_error("Cannot open predictive AMR output: " + path.string());
        // A writable path does not guarantee later writes succeed (for example
        // when storage fills). Fail at the recording boundary, not after claiming
        // a complete event whose rows were only buffered or discarded.
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream << header << '\n';
        stream << std::setprecision(17);
        stream.flush();
    }

    static double Center(const Grid& grid, int direction)
    {
        if (direction == 0) return 0.5 * (grid.x1_min + grid.x1_max);
        if (direction == 1) return 0.5 * (grid.x2_min + grid.x2_max);
        return 0.5 * (grid.x3_min + grid.x3_max);
    }

    static double Size(const Grid& grid, int direction)
    {
        if (direction == 0) return grid.x1_max - grid.x1_min;
        if (direction == 1) return grid.x2_max - grid.x2_min;
        return grid.x3_max - grid.x3_min;
    }

    static double PhysicalSpacing(const Grid& grid, int direction, int i, int j)
    {
        if (direction == 0) return grid.dx1;
        if (grid.geometry == "cartesian") return direction == 1 ? grid.dx2 : grid.dx3;

        const double radius = std::max(std::abs(grid.GetCellCenterX(i)), 0.5 * grid.dx1);
        if (grid.dim <= 2) return radius * grid.dx2;
        if (grid.geometry == "cylindrical")
            return direction == 1 ? grid.dx2 : radius * grid.dx3;

        if (direction == 1) return radius * grid.dx2;
        const double sin_theta = std::max(std::abs(std::sin(grid.GetCellCenterY(j))), 1.0e-12);
        return radius * sin_theta * grid.dx3;
    }

    static double ScalarGradientMagnitude(const Grid& grid,
                                          const std::vector<double>& values,
                                          int i, int j, int k)
    {
        const auto derivative = [&](int low, int high, int direction) {
            const double spacing = PhysicalSpacing(grid, direction, i, j);
            if (!(spacing > 0.0))
                throw std::runtime_error("Predictive AMR recorder encountered invalid grid spacing");
            return (values[high] - values[low]) / (2.0 * spacing);
        };
        const int center = grid.GetIndex(i, j, k);
        const double gx = derivative(grid.GetIndex(i - 1, j, k),
                                     grid.GetIndex(i + 1, j, k), 0);
        const double gy = grid.dim >= 2
            ? derivative(grid.GetIndex(i, j - 1, k), grid.GetIndex(i, j + 1, k), 1) : 0.0;
        const double gz = grid.dim == 3
            ? derivative(grid.GetIndex(i, j, k - 1), grid.GetIndex(i, j, k + 1), 2) : 0.0;
        (void)center;
        return std::sqrt(gx * gx + gy * gy + gz * gz);
    }

    void WriteManifest(const SimConfig& config)
    {
        const std::filesystem::path path = prefix_ + "_manifest.json";
        const std::filesystem::path parent = path.parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        std::ofstream manifest(path, std::ios::out | std::ios::trunc);
        if (!manifest)
            throw std::runtime_error("Cannot open predictive AMR manifest: " + path.string());
        manifest.exceptions(std::ios::badbit | std::ios::failbit);
        manifest << "{\n"
                 << "  \"schema_version\": " << kSchemaVersion << ",\n"
                 << "  \"observer_authority\": \"read_only\",\n"
                 << "  \"node_identity\": \"morton_level_logical_coordinates\",\n"
                 << "  \"graph_edges\": \"directed_face_neighbors\",\n"
                 << "  \"conservation_totals\": \"canonical_gridmetrics_cell_volume\",\n"
                 << "  \"geometry\": \"" << config.grid.geometry << "\",\n"
                 << "  \"dimension\": " << config.grid.dim << ",\n"
                 << "  \"compute_backend\": \"" << config.execution.compute_backend << "\",\n"
                 << "  \"lrefinemin\": " << config.amr.lrefinemin << ",\n"
                 << "  \"lrefinemax\": " << config.amr.lrefinemax << ",\n"
                 << "  \"regrid_interval\": " << config.amr.regrid_interval << ",\n"
                 << "  \"prediction_horizon_events\": "
                 << config.adaptive_runtime.predictive_amr_horizon << ",\n"
                 << "  \"history_events\": "
                 << config.adaptive_runtime.predictive_amr_history << ",\n"
                 << "  \"action_columns_are_targets_not_features\": true\n"
                 << "}\n";
        manifest.flush();
    }

    void WriteConservation(int step, double time, const char* phase,
                           const ConservedTotals& totals)
    {
        const auto write_value = [&](const char* quantity, int species_index,
                                     long double value) {
            conservation_ << kSchemaVersion << ',' << conservation_snapshots_ << ','
                          << event_index_ << ',' << step << ',' << time << ',' << phase << ','
                          << quantity << ',' << species_index << ','
                          << static_cast<double>(value) << '\n';
        };
        write_value("mass", -1, totals.mass);
        write_value("momentum_x1", -1, totals.momentum_x1);
        write_value("momentum_x2", -1, totals.momentum_x2);
        write_value("momentum_x3", -1, totals.momentum_x3);
        write_value("energy", -1, totals.energy);
        for (std::size_t species = 0; species < totals.species_mass.size(); ++species)
            write_value("species_mass", static_cast<int>(species), totals.species_mass[species]);
        conservation_.flush();
        ++conservation_snapshots_;
    }

public:
    PatchFeatureRecorder(const SimConfig& config,
                         std::shared_ptr<amr::MemoryPool> pool,
                         const EosPolicy& eos)
        : enabled_(config.adaptive_runtime.predictive_amr_record),
          pool_(std::move(pool)), eos_(&eos),
          small_density_(config.numerics.sml_rho),
          refine_threshold_(config.amr.refine_threshold),
          derefine_threshold_(config.amr.derefine_threshold)
    {
        if (!enabled_) return;
        if (!pool_) throw std::invalid_argument("Predictive AMR recorder requires the canonical MemoryPool");

        prefix_ = config.adaptive_runtime.predictive_amr_record_prefix.empty()
            ? (std::filesystem::path(config.io.out_dir) /
               (config.io.base_name + "_predictive_amr")).string()
            : config.adaptive_runtime.predictive_amr_record_prefix;

        Open(nodes_, prefix_ + "_nodes.csv",
             "schema_version,event_index,step,time,morton_code,block_id,level,logical_x1,logical_x2,logical_x3,center_x1,center_x2,center_x3,size_x1,size_x2,size_x3,refinement_indicator,criterion_action,balanced_action,balance_override,refine_margin,derefine_margin,neighbor_degree,neighbor_min_level,neighbor_max_level,boundary_mask,rho_min,rho_max,rho_mean,rho_std,pressure_min,pressure_max,pressure_mean,pressure_std,energy_min,energy_max,energy_mean,velocity_mean,velocity_max,grad_rho_max,grad_rho_mean,grad_pressure_max,grad_pressure_mean,abs_div_v_max,abs_div_v_mean,mach_max,mach_mean,compression_sensor_max,compression_sensor_mean,physical_cells");
        Open(edges_, prefix_ + "_edges.csv",
             "schema_version,event_index,step,time,edge_type,src_morton,src_level,face,dst_morton,dst_level,level_diff,relative_center_x1,relative_center_x2,relative_center_x3");
        Open(events_, prefix_ + "_events.csv",
             "schema_version,event_index,step,time,leaf_count,criterion_refine,criterion_coarsen,balanced_refine,balanced_coarsen,balance_overrides,node_rows,edge_rows,extract_ms,write_ms");
        Open(conservation_, prefix_ + "_conservation.csv",
             "schema_version,snapshot_index,event_index,step,time,phase,quantity,species_index,value");
        WriteManifest(config);
        std::cout << "[Adaptive Runtime] Read-only AMR recorder enabled: "
                  << prefix_ << std::endl;
    }

    bool enabled() const noexcept { return enabled_; }
    const std::string& prefix() const noexcept { return prefix_; }

    void Record(int step, double time, const amr::AmrTree& tree)
    {
        if (!enabled_) return;
        const auto record_begin = Clock::now();
        std::ostringstream node_buffer;
        std::ostringstream edge_buffer;
        node_buffer << std::setprecision(17);
        edge_buffer << std::setprecision(17);

        const auto& active_blocks = tree.GetActiveBlocks();
        std::size_t criterion_refine = 0;
        std::size_t criterion_coarsen = 0;
        std::size_t balanced_refine = 0;
        std::size_t balanced_coarsen = 0;
        std::size_t balance_overrides = 0;
        std::size_t event_node_rows = 0;
        std::size_t event_edge_rows = 0;
        ConservedTotals conserved;
        if (!active_blocks.empty())
            conserved.species_mass.assign(static_cast<std::size_t>(
                pool_->GetBlock(active_blocks.front()).fluid_state.GetNumSpecies()), 0.0L);

        for (const int block_id : active_blocks) {
            const amr::Block& block = pool_->GetBlock(block_id);
            const Grid& grid = block.grid;
            const FluidState& state = block.fluid_state;
            const int total_size = grid.GetTotalSize();
            const int species_count = state.GetNumSpecies();
            if (static_cast<std::size_t>(species_count) != conserved.species_mass.size())
                throw std::runtime_error("Predictive AMR recorder found inconsistent species counts");
            std::vector<double> pressure(total_size, 0.0);
            std::vector<double> sound_speed(total_size, 0.0);
            std::vector<double> velocity_x(total_size, 0.0);
            std::vector<double> velocity_y(total_size, 0.0);
            std::vector<double> velocity_z(total_size, 0.0);
            std::vector<double> Xi(species_count, 0.0);

            for (int index = 0; index < total_size; ++index) {
                for (int species = 0; species < species_count; ++species)
                    Xi[species] = state.X(species, index);
                const FluidVector U = state.get(index);
                pressure[index] = eos_->get_pressure(U, Xi.data());
                sound_speed[index] = eos_->get_sound_speed(U, pressure[index], Xi.data());
                const double denominator = std::max(U.rho, small_density_);
                velocity_x[index] = U.mom_u / denominator;
                velocity_y[index] = U.mom_v / denominator;
                velocity_z[index] = U.mom_w / denominator;
            }

            RunningStats density;
            RunningStats pressure_stats;
            RunningStats energy;
            RunningStats velocity;
            RunningStats grad_density;
            RunningStats grad_pressure;
            RunningStats absolute_divergence;
            RunningStats mach;
            RunningStats compression_sensor;

            for (int k = grid.Ks(); k < grid.Ke(); ++k) {
                for (int j = grid.Js(); j < grid.Je(); ++j) {
                    for (int i = grid.Is(); i < grid.Ie(); ++i) {
                        const int index = grid.GetIndex(i, j, k);
                        AccumulateConservedCell(conserved, state, grid, i, j, k);
                        density.Add(state.rho[index]);
                        pressure_stats.Add(pressure[index]);
                        energy.Add(state.eng[index]);
                        const double speed = std::sqrt(
                            velocity_x[index] * velocity_x[index] +
                            velocity_y[index] * velocity_y[index] +
                            velocity_z[index] * velocity_z[index]);
                        velocity.Add(speed);
                        grad_density.Add(ScalarGradientMagnitude(grid, state.rho, i, j, k));
                        grad_pressure.Add(ScalarGradientMagnitude(grid, pressure, i, j, k));
                        const VelocityDiagnostics::Values diagnostic =
                            VelocityDiagnostics::evaluate(grid, velocity_x, velocity_y, velocity_z, i, j, k);
                        absolute_divergence.Add(std::abs(diagnostic.divergence));
                        const double local_sound_speed = sound_speed[index];
                        if (!std::isfinite(local_sound_speed) || local_sound_speed <= 0.0)
                            throw std::runtime_error("Predictive AMR recorder requires positive finite sound speed");
                        mach.Add(speed / local_sound_speed);
                        double local_spacing = grid.dx1;
                        if (grid.dim >= 2)
                            local_spacing = std::min(local_spacing, PhysicalSpacing(grid, 1, i, j));
                        if (grid.dim == 3)
                            local_spacing = std::min(local_spacing, PhysicalSpacing(grid, 2, i, j));
                        const double acoustic_scale = local_sound_speed + speed +
                            std::numeric_limits<double>::min();
                        compression_sensor.Add(
                            std::max(0.0, -diagnostic.divergence) * local_spacing / acoustic_scale);
                    }
                }
            }

            int neighbor_degree = 0;
            int neighbor_min_level = std::numeric_limits<int>::max();
            int neighbor_max_level = std::numeric_limits<int>::min();
            int boundary_mask = 0;
            for (int face = 0; face < 2 * grid.dim; ++face) {
                const auto& neighbors = block.face_neighbors[face];
                if (neighbors.count == 0) boundary_mask |= (1 << face);
                for (int neighbor_index = 0; neighbor_index < neighbors.count; ++neighbor_index) {
                    const amr::Block& neighbor = pool_->GetBlock(neighbors.ids[neighbor_index]);
                    ++neighbor_degree;
                    neighbor_min_level = std::min(neighbor_min_level, neighbor.level);
                    neighbor_max_level = std::max(neighbor_max_level, neighbor.level);
                    edge_buffer << kSchemaVersion << ',' << event_index_ << ',' << step << ',' << time
                                << ",face," << block.morton_code << ',' << block.level << ',' << face << ','
                                << neighbor.morton_code << ',' << neighbor.level << ',' << neighbors.level_diff
                                << ',' << (Center(neighbor.grid, 0) - Center(grid, 0)) / Size(grid, 0)
                                << ',' << (grid.dim >= 2
                                    ? (Center(neighbor.grid, 1) - Center(grid, 1)) / Size(grid, 1) : 0.0)
                                << ',' << (grid.dim == 3
                                    ? (Center(neighbor.grid, 2) - Center(grid, 2)) / Size(grid, 2) : 0.0)
                                << '\n';
                    ++event_edge_rows;
                }
            }
            if (neighbor_degree == 0) {
                neighbor_min_level = -1;
                neighbor_max_level = -1;
            }

            criterion_refine += block.criterion_refine_flag == 1;
            criterion_coarsen += block.criterion_refine_flag == -1;
            balanced_refine += block.refine_flag == 1;
            balanced_coarsen += block.refine_flag == -1;
            balance_overrides += block.criterion_refine_flag != block.refine_flag;

            node_buffer << kSchemaVersion << ',' << event_index_ << ',' << step << ',' << time << ','
                        << block.morton_code << ',' << block_id << ',' << block.level << ','
                        << block.logical_x1 << ',' << block.logical_x2 << ',' << block.logical_x3 << ','
                        << Center(grid, 0) << ',' << Center(grid, 1) << ',' << Center(grid, 2) << ','
                        << Size(grid, 0) << ',' << Size(grid, 1) << ',' << Size(grid, 2) << ','
                        << block.refinement_indicator << ',' << block.criterion_refine_flag << ','
                        << block.refine_flag << ','
                        << (block.criterion_refine_flag != block.refine_flag ? 1 : 0) << ','
                        << block.refinement_indicator - refine_threshold_ << ','
                        << block.refinement_indicator - derefine_threshold_ << ','
                        << neighbor_degree << ',' << neighbor_min_level << ',' << neighbor_max_level << ','
                        << boundary_mask << ','
                        << density.Minimum() << ',' << density.Maximum() << ',' << density.Mean() << ','
                        << density.StandardDeviation() << ','
                        << pressure_stats.Minimum() << ',' << pressure_stats.Maximum() << ','
                        << pressure_stats.Mean() << ',' << pressure_stats.StandardDeviation() << ','
                        << energy.Minimum() << ',' << energy.Maximum() << ',' << energy.Mean() << ','
                        << velocity.Mean() << ',' << velocity.Maximum() << ','
                        << grad_density.Maximum() << ',' << grad_density.Mean() << ','
                        << grad_pressure.Maximum() << ',' << grad_pressure.Mean() << ','
                        << absolute_divergence.Maximum() << ',' << absolute_divergence.Mean() << ','
                        << mach.Maximum() << ',' << mach.Mean() << ','
                        << compression_sensor.Maximum() << ',' << compression_sensor.Mean() << ','
                        << density.Count() << '\n';
            ++event_node_rows;
        }

        const auto extract_end = Clock::now();
        const auto write_begin = extract_end;
        nodes_ << node_buffer.str();
        edges_ << edge_buffer.str();
        nodes_.flush();
        edges_.flush();
        WriteConservation(step, time, "pre_regrid", conserved);
        const auto write_end = Clock::now();
        events_ << kSchemaVersion << ',' << event_index_ << ',' << step << ',' << time << ','
                << active_blocks.size() << ',' << criterion_refine << ',' << criterion_coarsen << ','
                << balanced_refine << ',' << balanced_coarsen << ',' << balance_overrides << ','
                << event_node_rows << ',' << event_edge_rows << ','
                << ElapsedMs(record_begin, extract_end) << ',' << ElapsedMs(write_begin, write_end) << '\n';
        events_.flush();

        observer_ms_ += ElapsedMs(record_begin, Clock::now());
        node_rows_ += event_node_rows;
        edge_rows_ += event_edge_rows;
        ++event_index_;
    }

    void RecordFinal(int step, double time, const amr::AmrTree& tree)
    {
        if (!enabled_) return;
        const auto begin = Clock::now();
        WriteConservation(step, time, "final", ComputeConservedTotals(tree));
        observer_ms_ += ElapsedMs(begin, Clock::now());
    }

    void PrintSummary() const
    {
        if (!enabled_) return;
        std::cout << std::fixed << std::setprecision(3)
                  << "[ADAPTIVE RUNTIME DATA] events=" << event_index_
                  << " nodes=" << node_rows_
                  << " edges=" << edge_rows_
                  << " conservation_snapshots=" << conservation_snapshots_
                  << " observer_ms=" << observer_ms_
                  << " prefix=" << prefix_
                  << std::defaultfloat << std::endl;
    }
};

} // namespace arch::runtime::predictive_amr
