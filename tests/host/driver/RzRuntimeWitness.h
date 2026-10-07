/**
 * @file RzRuntimeWitness.h
 * @brief Shared test-only bit/lease and receipt witnesses for real RZ owners.
 *
 * Workflow:
 * 1. Freeze actual Current/Next/Scratch arrays, allocations and observer aliases.
 * 2. Compare every original scalar bit, repair receipt and capture plane.
 * 3. Apply the existing local scalar check without changing its 2e-12 budget.
 *
 * This header contains diagnostic witnesses only. Native moment references,
 * EOS, source/flux math and the actual Runtime integrators remain with their
 * sole existing owners; no duplicated scientific implementation is introduced.
 */
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <vector>

#include "amr/storage/Block.h"
#include "data/FluidState.h"
#include "data/StateDiagnostics.h"
#include "physics/boundary/BoundaryDiagnostics.h"

namespace rz_runtime_witness {
using namespace arch;
/** Reject a failed existing assertion; no tolerance or acceptance is supplied. */
inline void require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
using Field=std::vector<double> FluidState::*;
inline constexpr std::array<Field,7> fields{&FluidState::rho,&FluidState::mom_u,
    &FluidState::mom_v,&FluidState::mom_w,&FluidState::eng,
    &FluidState::enuc_rate,&FluidState::mass_fractions};
inline constexpr double scalar_budget=2.e-12; // Existing owner's unchanged scalar budget.

inline bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
inline bool bits(const std::vector<double>& a,const std::vector<double>& b) {
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),
        [](double x,double y){return bits(x,y);});
}
inline void near(double actual,double expected,const char* message) {
    require(std::isfinite(actual)&&std::abs(actual-expected)
        <=scalar_budget*std::max(1.,std::abs(expected)),message);
}
inline bool same_repairs(const state::RepairBudget& a,const state::RepairBudget& b) {
    return a.semantics==b.semantics&&bits(a.values,b.values)
        &&a.block_uid==b.block_uid&&a.stage==b.stage&&bits(a.time,b.time)
        &&std::equal(std::begin(a.position),std::end(a.position),std::begin(b.position),
            [](double x,double y){return bits(x,y);});
}
inline std::array<FluidState*,3> slots(amr::Block& b){return {&b.fluid_state,&b.state_next,&b.state_scratch};}

/** Snapshot values and original allocation/alias ownership independently of rollback. */
struct FieldsWitness {
    std::array<FluidState,3> values;
    std::array<std::array<const double*,7>,3> leases{};
    std::array<std::optional<boundary::BoundaryFluxCaptureStorage>,3> captures;
    explicit FieldsWitness(amr::Block& b):values{b.fluid_state,b.state_next,b.state_scratch} {
        const auto actual=slots(b);
        for(int s=0;s<3;++s) {
            for(int f=0;f<7;++f)leases[s][f]=(actual[s]->*fields[f]).data();
            if(actual[s]->boundary_flux_capture)captures[s]=*actual[s]->boundary_flux_capture;
        }
    }
    void matches(amr::Block& b) const {
        const auto actual=slots(b);
        for(int s=0;s<3;++s) {
            const auto& a=*actual[s];const auto& saved=values[s];
            require(a.n_species_==saved.n_species_&&a.block_total_size_==saved.block_total_size_,
                "angular macro rollback changed an actual slot layout");
            for(int f=0;f<7;++f)require((a.*fields[f]).data()==leases[s][f]
                &&bits(a.*fields[f],saved.*fields[f]),
                "angular macro rollback changed source/output/ghost/padding bits or a seven-array lease");
            require(same_repairs(a.stage_repairs,saved.stage_repairs)
                &&a.diffusion_boundary==saved.diffusion_boundary
                &&a.boundary_flux_capture==saved.boundary_flux_capture,
                "angular macro rollback changed native receipts or control/capture alias ownership");
            require(bool(a.boundary_flux_capture)==bool(captures[s]),
                "angular macro rollback changed capture presence");
            if(captures[s]) {
                const auto& now=*a.boundary_flux_capture;const auto& old=*captures[s];
                for(int face=0;face<6;++face)require(bits(now.stage[face],old.stage[face])
                    &&bits(now.initial[face],old.initial[face]),"angular macro rollback changed shared capture planes");
                require(bits(now.weight,old.weight)&&bits(now.initial_weight,old.initial_weight)
                    &&now.save_initial==old.save_initial,"angular macro rollback changed capture weights");
            }
        }
    }
};

} // namespace rz_runtime_witness
