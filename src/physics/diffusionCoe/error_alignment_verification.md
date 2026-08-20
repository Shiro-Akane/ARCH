# Diffusion-Coefficient Mathematical Alignment

Chinese translation: [error_alignment_verification.zh-CN.md](error_alignment_verification.zh-CN.md).
The English file is the authoritative source text.

## Source basis

`diffusion_math.hpp` is the framework-independent ARCH adaptation of
AMReX-Astro Microphysics
[`conductivity/stellar/actual_conductivity.H`](https://github.com/AMReX-Astro/Microphysics/blob/6fb41b5f7475b42a06eb5b09ff0520c9f08aa7f0/conductivity/stellar/actual_conductivity.H),
audited at commit `6fb41b5f7475b42a06eb5b09ff0520c9f08aa7f0`.
The retained license and adaptation boundary are recorded in
[`THIRD_PARTY_NOTICES.md`](../../../THIRD_PARTY_NOTICES.md).

## Validation scope

This test verifies the numerical output of `ConductivityMath` in
`diffusion_math.hpp`. The evaluated call path is:

```text
HelmEos::evaluate
  -> eos_state_t
  -> SpeciesManager Aion^-1 and Zion data
  -> ConductivityMath::compute_stellar_conductivity
```

The sampled parameter space covers:

- temperature: `1e7--1e9 K`;
- density: `1e4--1e9 g cm^-3`;
- composition network: `aprox19`;
- sample count: more than 10,000 randomized states.

## Comparison method

Both the reference path and the `ConductivityMath` path use IEEE 754 double
precision. Results are emitted with `std::hexfloat` and compared by their
underlying floating-point values.

The conductivity values for three representative states are:

```text
State 1: 0x1.204aa662ac9cfp+32
State 2: 0x1.27bbef6acfaf3p+49
State 3: 0x1.08d38f0ecf089p+58
```

## Results

| Metric | Result |
| --- | ---: |
| Maximum absolute error | `0.0` |
| Maximum relative error | `0.0` |

The two conductivity paths are bitwise identical over the tested range. This
claim is limited to the stated parameter range, composition network, and test
build. Any extension of the physical model or numerical path requires a
corresponding validation record.
