// Independent observation of the production Driver's initial checkpoint.
#include "api/protocol/Json.h"
#include <highfive/H5File.hpp>
#include <cstdint>
#include <iostream>
#include <vector>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    HighFive::File file(argv[1], HighFive::File::ReadOnly);
    auto blocks = file.getGroup("Blocks");
    std::vector<int> levels;
    std::vector<std::uint32_t> x, y, z;
    blocks.getDataSet("level").read(levels);
    blocks.getDataSet("logical_x1").read(x);
    blocks.getDataSet("logical_x2").read(y);
    blocks.getDataSet("logical_x3").read(z);
    auto keys = arch::api::detail::Json::array();
    for (std::size_t i = 0; i < levels.size(); ++i)
        keys.push(std::to_string(levels[i])+":"+std::to_string(x[i])+":"+std::to_string(y[i])+":"+std::to_string(z[i]));
    double time = -1; file.getAttribute("time").read(time);
    std::cout << arch::api::detail::Json::object({{"keys", keys}, {"time", time}}).dump() << '\n';
}
