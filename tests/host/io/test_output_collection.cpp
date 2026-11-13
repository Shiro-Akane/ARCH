/** Integrity and incomplete-publication failures for native output collections. */
#include "io/OutputCollection.h"
#include "core/config/RuntimeParams.h"
#include "core/files/FileFingerprint.h"
#include "core/files/RunIdentity.h"
#include <algorithm>
#include <bit>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
int rejected = 0;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Work> void rejects(Work&& work) {
    try { work(); } catch (const std::exception&) { ++rejected; return; }
    throw std::runtime_error("Invalid output collection was accepted");
}
void save(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary); out << text; out.close();
    require(bool(out), "test file write failed");
}
std::string load(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
struct Fixture {
    fs::path root = fs::temp_directory_path() / ("arch-collection-" + arch::core::new_run_identity());
    Fixture() { require(fs::create_directory(root), "test directory already exists"); }
    ~Fixture() { std::error_code ignored; fs::remove_all(root, ignored); }
};
}
int main() {
    try {
        Fixture fixture;
        SimConfig original_domain;
        original_domain.grid.dim=2;original_domain.grid.nblockx1=4;original_domain.grid.nblockx2=1;
        original_domain.amr.lrefinemax=2;
        auto changed_domain=original_domain;
        changed_domain.io.out_dir="different-output";changed_domain.io.tmax+=10.;
        require(io::output_domain_sha256(original_domain)==io::output_domain_sha256(changed_domain),
            "continuation output/time controls changed root identity");
        changed_domain.grid.x1_max+=1.;
        require(io::output_domain_sha256(original_domain)!=io::output_domain_sha256(changed_domain),
            "root extent change escaped restart identity");
        std::vector<amr::LogicalBlockKey> leaves{{2,1,0,0,0},{2,1,1,0,0}};
        auto reordered=leaves;std::reverse(reordered.begin(),reordered.end());
        require(io::output_topology_sha256(leaves)==io::output_topology_sha256(reordered),
            "local storage ordering changed logical checkpoint identity");
        reordered.front().logical_x1=2;
        require(io::output_topology_sha256(leaves)!=io::output_topology_sha256(reordered),
            "different leaves shared the topology identity");
        rejects([&]{io::output_topology_sha256({});});
        reordered=leaves;reordered.back()=reordered.front();
        rejects([&]{io::output_topology_sha256(reordered);});
        for (int ranks : {2, 4}) for (const auto kind : {"plot", "checkpoint"}) {
            const fs::path stage = fixture.root / (std::string(kind) + "-" + std::to_string(ranks) + ".partial");
            fs::create_directory(stage);
            io::OutputCollection value;
            value.kind = kind; value.run_id = arch::core::new_run_identity();
            value.binary_sha256 = arch::core::string_sha256("binary");
            value.configuration_sha256 = arch::core::string_sha256("configuration");
            value.eos_sha256 = arch::core::string_sha256("eos");
            value.topology_sha256 = arch::core::string_sha256("topology");
            value.domain_sha256 = arch::core::string_sha256("domain");
            value.output_index = 7; value.step = 31; value.time = .125;
            for (int rank = 0; rank < ranks; ++rank) {
                const auto path = io::output_shard_path(stage, rank);
                fs::create_directory(path.parent_path());
                save(path, "native shard " + std::to_string(rank));
                value.shards.push_back({static_cast<std::uint64_t>(rank + 1),
                    fs::file_size(path), arch::core::file_sha256(path.string())});
            }
            io::write_output_collection(stage, value);
            io::verify_output_collection(stage, value);
            rejects([&] { io::read_output_collection(stage); });
            rejects([&] { io::write_output_collection(stage, value); });
            const auto published = fs::path(stage.string() + ".collection");
            const auto last=io::output_shard_path(stage,ranks-1);
            fs::rename(last,stage/"held.h5");
            rejects([&]{io::publish_output_collection(stage,published);});
            require(!fs::exists(published)&&fs::is_directory(stage),
                "incomplete publication changed accepted namespace");
            fs::rename(stage/"held.h5",last);
            io::publish_output_collection(stage, published);
            fs::copy(published, stage, fs::copy_options::recursive);
            rejects([&] { io::publish_output_collection(stage, published); });
            require(fs::is_directory(stage), "failed publication consumed staging evidence");
            rejects([&] { io::publish_output_collection(stage, fixture.root / "unpublished.partial"); });
            auto restored = io::read_output_collection(published);
            io::verify_output_collection(published, restored);
            require(restored.kind == value.kind && restored.run_id == value.run_id
                && restored.configuration_sha256 == value.configuration_sha256
                && restored.binary_sha256 == value.binary_sha256
                && restored.eos_sha256 == value.eos_sha256
                && restored.topology_sha256 == value.topology_sha256
                && restored.domain_sha256 == value.domain_sha256
                && restored.step == value.step && restored.output_index == value.output_index
                && std::bit_cast<std::uint64_t>(restored.time) == std::bit_cast<std::uint64_t>(value.time)
                && restored.shards.size() == value.shards.size(), "collection identity changed");
            const auto manifest = published / "manifest.txt";
            const auto original = load(manifest);
            const auto malformed = [&](const std::string& from, const std::string& to) {
                auto text = original;
                const auto pos = text.find(from); require(pos != std::string::npos, "missing corruption target");
                text.replace(pos, from.size(), to); save(manifest, text);
                rejects([&] { io::read_output_collection(published); }); save(manifest, original);
            };
            malformed("COLLECTION 1", "COLLECTION 2");
            malformed("shard 1 ", "shard 0 ");
            malformed("step 31", "step -1");
            malformed("kind " + value.kind, "kind other");
            malformed("time_bits " + std::to_string(std::bit_cast<std::uint64_t>(value.time)),
                "time_bits " + std::to_string(std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN())));
            malformed("ranks " + std::to_string(ranks), "ranks 18446744073709551615");
            malformed("ranks " + std::to_string(ranks), "ranks 0");
            malformed("end\n", "end\ntrailing\n");
            malformed("shard 0 1 ", "shard 0 0 ");
            malformed(value.binary_sha256, "invalid-digest");
            save(manifest, original.substr(0, original.size() / 2));
            rejects([&] { io::read_output_collection(published); }); save(manifest, original);
            const auto first = io::output_shard_path(published, 0);
            const auto second = io::output_shard_path(published, 1);
            const auto first_data = load(first), second_data = load(second);
            save(first, "X" + first_data.substr(1));
            rejects([&] { io::verify_output_collection(published, restored); }); save(first, first_data);
            save(first, first_data.substr(1));
            rejects([&] { io::verify_output_collection(published, restored); }); save(first, first_data);
            save(first, second_data); save(second, first_data);
            rejects([&] { io::verify_output_collection(published, restored); });
            save(first, first_data); save(second, second_data);
            fs::rename(second, published / "saved.h5");
            rejects([&] { io::verify_output_collection(published, restored); });
#if defined(_WIN32)
            // Windows symlink creation may require privileges; a directory
            // exercises the same non-regular shard rejection without them.
            fs::create_directory(second);
#else
            fs::create_symlink(published / "saved.h5", second);
#endif
            rejects([&] { io::verify_output_collection(published, restored); });
            fs::remove(second); fs::rename(published / "saved.h5", second);
            io::verify_output_collection(published, restored);
        }
        require(rejected == 86, "collection failure coverage changed");
        std::cout << "PASS output collections layouts=4 integrity_rejections=" << rejected << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
