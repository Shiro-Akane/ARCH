#pragma once

#include "amr/transfer/LimitedLinearProlongation.h"

#include <array>

namespace amr::test {

// Every input cell is normalized. Independent MC limiting gives left
// fractions (0.3,0.475,0.2,1e-20), whose sum is 0.975, before normalization.
inline constexpr std::array<std::array<double, 4>, 4> muscl_composition_cells{{
    {0.1, 0.8, 0.1, 1e-20}, {0.2, 0.6, 0.2, 1e-20},
    {0.5, 0.3, 0.2, 1e-20}, {0.7, 0.1, 0.2, 1e-20}}};
inline constexpr std::array<double, 8> muscl_composition_faces{
    0.3/0.975, 0.475/0.975, 0.2/0.975, 1e-20/0.975,
    0.375, 0.425, 0.2, 1e-20};

// One center followed by the lower/upper sample of each axis.  The same
// fixtures exercise the Host math and the production CUDA memory kernels.
struct CompositionCase {
    static constexpr int cells = 7;
    static constexpr int species = 4;
    std::array<double, cells> density{};
    std::array<double, species * cells> fractions{};
    std::array<int, 6> slopes{1, 2, 3, 4, 5, 6};
    std::array<prolongation_math::CompositionFamily, 3> family{
        prolongation_math::CompositionFamily::Linear,
        prolongation_math::CompositionFamily::Linear,
        prolongation_math::CompositionFamily::Linear};
    bool preserve_last_trace = false;

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

inline std::array<CompositionCase, 7> composition_cases()
{
    std::array<CompositionCase, 7> cases;
    for (int cell = 0; cell < CompositionCase::cells; ++cell) {
        cases[0].set_cell(cell, 1.0, {0.3, 0.6, 0.1, 0.0});
        cases[1].set_cell(cell, 1.0, {0.2, 0.3, 0.5 - 1.0e-13, 1.0e-13});
        cases[2].set_cell(cell, 1.0, {0.25, 0.25, 0.25, 0.25});
        cases[3].set_cell(cell, 1.0, {0.3, 0.6, 0.1, 0.0});
        cases[4].set_cell(cell, 1.0, {0.3, 0.6, 0.1, 1.0e-20});
        cases[5].set_cell(cell, 1.0, {0.25, 0.25, 0.25, 0.25});
        cases[6].set_cell(cell, 1.0, {0.3, 0.6, 0.1, 1.0e-20});
    }
    // A nonuniform admissible family, followed by a stencil whose old
    // last-species closure was negative. Dominant-species closure is valid
    // for that stencil, so it no longer needs the constant fallback.
    cases[2].set_cell(1, 0.5, {0.25, 0.25, 0.25, 0.25});
    cases[2].set_cell(2, 1.5, {0.25, 0.25, 0.25, 0.25});
    cases[3].set_cell(1, 0.5, {0.2, 0.4, 0.4, 0.0});
    cases[3].set_cell(2, 1.5, {7.0 / 15.0, 7.0 / 15.0, 1.0 / 15.0, 0.0});
    cases[0].preserve_last_trace = true;
    cases[4].preserve_last_trace = true;
    cases[6].preserve_last_trace = true;
    cases[4].set_cell(1, 0.5, {0.3, 0.6, 0.1, 1.0e-20});
    cases[4].set_cell(2, 1.5, {0.3, 0.6, 0.1, 1.0e-20});
    // Independent true fallback: the nonclosure rhoX slopes are zero,
    // while rho has slope 0.9 along each active axis. For the -- sibling
    // in 2D rho=0.55 < sum(nonclosure rhoX)=0.75. All parent/stencil
    // fractions are normalized and nonnegative. In 1D rho>=0.775 is valid.
    for (int axis=0; axis<3; ++axis) {
        cases[5].set_cell(1+2*axis, 0.1, {0.25, 0.25, 0.25, 0.25});
        cases[5].set_cell(2+2*axis, 2.0, {0.85, 0.05, 0.05, 0.05});
    }
    cases[5].family[1] = cases[5].family[2] =
        prolongation_math::CompositionFamily::Constant;
    return cases;
}

} // namespace amr::test
