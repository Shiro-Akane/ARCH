/**
 * @file test_jeans_diagnostics.cpp
 * @brief CPU numeric qualification of the isolated Jeans formula.
 * Independent Decimal 80/120-digit references use published pi and CGS G.
 * The 16-double-epsilon check is an engineering rounding check, not an EOS,
 * AMR threshold, trajectory or scientific acceptance budget.
 */
#include "physics/diagnostics/JeansDiagnostics.h"
#include <array>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <algorithm>

int main()
{
    struct Case { double rho, cs2, h, expected; };
    const std::array<Case, 8> cases{{
        {0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x1.accc1f12f6cbdp+12},
        {0x1.0000000000000p+2, 0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x1.accc1f12f6cbdp+11},
        {0x1.0000000000000p+0, 0x1.0000000000000p+2, 0x1.0000000000000p+0, 0x1.accc1f12f6cbdp+13},
        {0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x1.0000000000000p+2, 0x1.accc1f12f6cbdp+10},
        {0x0.00000000007e8p-1022, 0x1.56e1fc2f8f359p-997, 0x1.5af1d78b58c40p+66, 0x1.70560f248ababp-21},
        {0x1.7e43c8800759cp+996, 0x1.7e43c8800759cp+996, 0x1.0000000000000p+0, 0x1.accc1f12f6cbdp+12},
        {0x1.56e1fc2f8f359p-997, 0x1.7e43c8800759cp+996, 0x1.7e43c8800759cp+996, 0x1.accc1f12f6cbcp+12},
        {0x1.7e43c8800759cp+996, 0x1.56e1fc2f8f359p-997, 0x1.56e1fc2f8f359p-997, 0x1.accc1f12f6cbcp+12}
    }};
    for (const auto& c : cases) {
        const auto result = JeansDiagnostics::evaluate(c.rho, c.cs2, c.h);
        if (result.status != JeansDiagnostics::Status::valid ||
            std::abs(result.cells - c.expected) >
                16 * std::numeric_limits<double>::epsilon() * c.expected)
            throw std::runtime_error("Jeans numeric reference mismatch");
    }
    for (double invalid : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        for (const auto& args : {std::array<double,3>{invalid,1,1},
                                std::array<double,3>{1,invalid,1},
                                std::array<double,3>{1,1,invalid}})
            if (JeansDiagnostics::evaluate(args[0],args[1],args[2]).status !=
                JeansDiagnostics::Status::invalid_input)
                throw std::runtime_error("Invalid Jeans numeric input accepted");
    }
    const auto overflow = JeansDiagnostics::evaluate(1e-300,1e300,1e-300);
    const auto underflow = JeansDiagnostics::evaluate(1e300,1e-300,1e300);
    if (overflow.status != JeansDiagnostics::Status::unrepresentable ||
        underflow.status != JeansDiagnostics::Status::unrepresentable)
        throw std::runtime_error("Jeans nonrepresentable result hidden");
    std::cout << "JEANS_DIAGNOSTICS_NUMERIC_PASS\n";
}
