#pragma once

#include "amr/LimitedLinearProlongation.h"

#include <array>

namespace amr::test {

// One center followed by the lower/upper sample of each axis.  The same
// fixtures exercise the Host math and the production CUDA memory kernels.
struct CompositionCase {
    static constexpr int cells = 7;
    static constexpr int species = 4;
    std::array<double, cells> density{};
    std::array<double, species * cells> fractions{};
    std::array<int, 6> slopes{1, 2, 3, 4, 5, 6};
    prolongation_math::CompositionFamily family =
        prolongation_math::CompositionFamily::Linear;

    prolongation_math::CompositionStencilView stencil(int dimension) const
    {
        return {density.data(), fractions.data(), cells, 0, slopes.data(),
                dimension, species};
    }

    void set_cell(int cell, double rho, const std::array<double, species>& x)
    {
        density[cell] = rho;
        for (int component = 0; component < species; ++component)
            fractions[component * cells + cell] = x[component];
    }
};

inline std::array<CompositionCase, 4> composition_cases()
{
    std::array<CompositionCase, 4> cases;
    for (int cell = 0; cell < CompositionCase::cells; ++cell) {
        cases[0].set_cell(cell, 1.0, {0.3, 0.6, 0.1, 0.0});
        cases[1].set_cell(cell, 1.0, {0.2, 0.3, 0.5 - 1.0e-13, 1.0e-13});
        cases[2].set_cell(cell, 1.0, {0.25, 0.25, 0.25, 0.25});
        cases[3].set_cell(cell, 1.0, {0.3, 0.6, 0.1, 0.0});
    }
    // A nonuniform but admissible family, followed by one in which the
    // independently limited rhoX slopes imply negative closure for -X.
    cases[2].set_cell(1, 0.5, {0.25, 0.25, 0.25, 0.25});
    cases[2].set_cell(2, 1.5, {0.25, 0.25, 0.25, 0.25});
    cases[3].set_cell(1, 0.5, {0.2, 0.4, 0.4, 0.0});
    cases[3].set_cell(2, 1.5, {7.0 / 15.0, 7.0 / 15.0, 1.0 / 15.0, 0.0});
    cases[3].family = prolongation_math::CompositionFamily::Constant;
    return cases;
}

} // namespace amr::test
