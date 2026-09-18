/**
 * @file test_predictive_amr_recorder.cpp
 * @brief Verify read-only AMR recording, configuration and I/O failures.
 *
 * Exercises canonical labels, rollback and storage reuse on the Host. Actual
 * device materialization is covered by the complete-application regression.
 */
#include "core/RuntimeParams.h"
#include "physics/eos/IdealGas.h"
#include "runtime/predictive_amr/PatchFeatureRecorder.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

std::string read(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    require(static_cast<bool>(stream), "missing recorder artifact");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void parameters(const std::filesystem::path& directory)
{
    const auto file = directory / "recorder.par";
    const auto load = [&](const std::string& text) {
        { std::ofstream output(file); output << text; }
        return RuntimeParams::Load(file.string()).adaptive_runtime;
    };
    const auto defaults = load("");
    require(!defaults.predictive_amr_record && defaults.predictive_amr_horizon == 4
            && defaults.predictive_amr_history == 4, "observer defaults changed");
    for (const auto* value : {"true", "TRUE", "True"})
        require(load(std::string("predictive_amr_record = ") + value).predictive_amr_record,
                "enabled observer spelling rejected");
    for (const auto* value : {"false", "FALSE", "False"})
        require(!load(std::string("predictive_amr_record = ") + value).predictive_amr_record,
                "disabled observer spelling rejected");
    const auto explicit_values = load("predictive_amr_record_prefix = preserved-prefix\n"
        "predictive_amr_horizon = 8\npredictive_amr_history = 0\n");
    require(explicit_values.predictive_amr_record_prefix == "preserved-prefix"
        && explicit_values.predictive_amr_horizon == 8
        && explicit_values.predictive_amr_history == 0, "observer parameters lost");
    for (const auto* invalid : {"predictive_amr_record = 0\n",
            "predictive_amr_record = 1\n", "predictive_amr_record = 2\n",
            "predictive_amr_horizon = 0\n", "predictive_amr_history = -1\n"}) {
        bool rejected = false;
        try { (void)load(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid observer configuration accepted");
    }
}

void recorder_contract(const std::filesystem::path& directory, int dimension)
{
    SimConfig config{};
    config.grid.dim = dimension;
    config.grid.nblockx1 = 1;
    config.grid.nblockx2 = dimension >= 2 ? 1 : 0;
    config.grid.nblockx3 = dimension == 3 ? 1 : 0;
    config.grid.amr_max_blocks = 16;
    config.amr.lrefinemax = 1;
    config.amr.refine_on_rho = true;
    config.amr.refine_threshold = 0.1;
    config.amr.derefine_threshold = 0.0;
    auto pool = std::make_shared<amr::MemoryPool>(16, dimension);
    amr::AmrTree tree(pool);
    tree.InitRootGrid(config, 1);
    auto& block = pool->GetBlock(tree.GetActiveBlocks().front());
    auto& state = block.fluid_state;
    std::fill(state.rho.begin(), state.rho.end(), 1.0);
    std::fill(state.eng.begin(), state.eng.end(), 10.0);
    for (int index = 0; index < block.grid.GetTotalSize(); ++index) state.X(0, index) = 1.0;
    // A genuine nonuniform criterion, including filled ghost storage.
    state.rho[block.grid.GetIndex(block.grid.Is() + 1, block.grid.Js(), block.grid.Ks())] = 2.0;
    const auto density = state.rho;
    const auto energy = state.eng;
    const auto composition = state.mass_fractions;
    const auto active = tree.GetActiveBlocks();
    IdealGasView eos{};
    using Recorder = arch::runtime::predictive_amr::PatchFeatureRecorder<IdealGasView>;
    // No pool or files are needed while disabled.
    Recorder disabled(config, nullptr, eos);
    require(!disabled.enabled() && disabled.prefix().empty(), "disabled observer performed setup");
    disabled.Record(0, 0.0, tree);
    disabled.RecordFinal(0, 0.0, tree);

    config.adaptive_runtime.predictive_amr_record = true;
    const auto prefix = (directory / ("dim" + std::to_string(dimension))).string();
    config.adaptive_runtime.predictive_amr_record_prefix = prefix;
    block.refinement_indicator = 0.125;
    block.criterion_refine_flag = -1;
    const auto prior_flag = block.refine_flag;
    {
        Recorder recorder(config, pool, eos);
        bool observed = false;
        {
            auto staged = tree.PrepareRegrid(config, [&](const amr::AmrTree& snapshot) {
                require(snapshot.GetActiveBlocks() == active, "observer ran after topology publication");
                require(block.refinement_indicator > 0.0, "canonical indicator not captured");
                require(block.criterion_refine_flag == amr::indicator::refinement_flag(
                    block.refinement_indicator, block.level, config.amr.lrefinemin,
                    config.amr.lrefinemax, config.amr.refine_threshold,
                    config.amr.derefine_threshold), "criterion label differs from shared authority");
                recorder.Record(3, 0.25, snapshot);
                observed = true;
            });
            require(observed && staged.topology_changed(), "refine observer did not run");
            staged.AbortNoexcept();
        }
        require(block.refinement_indicator == 0.125 && block.criterion_refine_flag == -1
            && block.refine_flag == prior_flag, "transaction lost diagnostic rollback");
        require(tree.GetActiveBlocks() == active, "observer changed topology");
        require(state.rho == density && state.eng == energy
            && state.mass_fractions == composition, "observer mutated fields");

        // Exercise the Host-lowered device-indicator callback contract, not a GPU claim.
        bool lowered_observed = false;
        {
            auto staged = tree.PrepareRegrid(config,
                [&](const amr::AmrTree& snapshot) {
                    require(block.refinement_indicator == 0.75 && block.criterion_refine_flag == 1,
                            "custom indicator callback overwritten by host evaluation");
                    recorder.Record(4, 0.5, snapshot);
                    lowered_observed = true;
                }, {}, [&] {
                    block.refinement_indicator = 0.75;
                    block.criterion_refine_flag = block.refine_flag = 1;
                });
        }
        require(lowered_observed, "lowered observer callback missing");
        bool threw = false;
        try {
            auto staged = tree.PrepareRegrid(config, [](const amr::AmrTree&) {
                throw std::runtime_error("injected observer failure");
            });
        } catch (const std::runtime_error&) { threw = true; }
        require(threw && block.refinement_indicator == 0.125
            && block.criterion_refine_flag == -1 && block.refine_flag == prior_flag,
            "throwing observer corrupted staged state");
        recorder.RecordFinal(5, 0.75, tree);
        recorder.PrintSummary();
    }
    require(read(prefix + "_manifest.json").find("\"schema_version\": 2") != std::string::npos,
            "Phase 0 schema changed");
    require(read(prefix + "_nodes.csv").find("criterion_action,balanced_action,balance_override")
        != std::string::npos, "action labels removed");
    require(read(prefix + "_edges.csv").find("directed") == std::string::npos,
            "unexpected edge schema mutation");
    require(read(prefix + "_events.csv").find("2,0,3,0.25") != std::string::npos,
            "event record missing");
    const auto conservation = read(prefix + "_conservation.csv");
    require(conservation.find(",pre_regrid,mass,-1,") != std::string::npos
        && conservation.find(",final,species_mass,0,") != std::string::npos,
        "pre-regrid/final conservation records missing");
#if defined(__linux__)
    // /dev/full accepts open but rejects writes: exercise failure after opening,
    // rather than treating an unwritable parent directory as the same condition.
    if (std::filesystem::exists("/dev/full")) {
        const auto failed_prefix = prefix + "_write_failure";
        const auto failed_nodes = failed_prefix + "_nodes.csv";
        std::filesystem::create_symlink("/dev/full", failed_nodes);
        config.adaptive_runtime.predictive_amr_record_prefix = failed_prefix;
        bool rejected = false;
        try { Recorder failed(config, pool, eos); }
        catch (const std::ios_base::failure&) { rejected = true; }
        std::filesystem::remove(failed_nodes);
        require(rejected, "recorder silently accepted a failed output write");
    }
#endif
    block.Reset();
    require(block.refinement_indicator == 0.0 && block.criterion_refine_flag == 0,
            "pool recycling retained stale diagnostic labels");
}
}

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "expected isolated output directory");
        const std::filesystem::path directory = argv[1];
        std::filesystem::create_directories(directory);
        parameters(directory);
        for (int dimension : {1, 2, 3}) recorder_contract(directory, dimension);
        std::cout << "predictive AMR recorder contracts passed (Host; no GPU qualification)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
