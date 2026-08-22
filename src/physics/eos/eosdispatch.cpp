/**
 * @file eosdispatch.cpp
 * @brief HDF5 rank inspection kept out of the templated EOS dispatcher.
 */

#include "eosdispatch.h"

#include <stdexcept>

#include <highfive/H5File.hpp>

int inspect_eos_table_rank(const std::string& path)
{
    HighFive::File file(path, HighFive::File::ReadOnly);

    if (file.exist("table_rank")) {
        int rank = 0;
        file.getDataSet("table_rank").read(rank);
        if (rank != 3 && rank != 4) {
            throw std::runtime_error(
                "Tabular EOS table_rank must be exactly 3 or 4");
        }
        const bool has_A = file.exist("n_A");
        const bool has_Z = file.exist("n_Z");
        const bool has_X = file.exist("n_X");
        const bool valid_3d = rank == 3 && has_X && !has_A && !has_Z;
        const bool valid_4d = rank == 4 && has_A && has_Z && !has_X;
        if (!valid_3d && !valid_4d) {
            throw std::runtime_error(
                "Tabular EOS table_rank disagrees with its composition axes");
        }
        return rank;
    }

    const bool has_A = file.exist("n_A");
    const bool has_Z = file.exist("n_Z");
    const bool has_X = file.exist("n_X");
    if (has_A != has_Z || (has_X && has_A)) {
        throw std::runtime_error(
            "Legacy tabular EOS axes are incomplete or ambiguous");
    }
    if (has_A && has_Z) return 4;
    if (has_X) return 3;
    throw std::runtime_error(
        "Cannot infer tabular EOS rank: provide table_rank or legacy n_X/n_A/n_Z datasets");
}
