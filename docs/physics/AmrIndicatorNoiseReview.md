# Open shared AMR indicator noise-sensitivity review

Status: diagnosed during the 2026-09-05 short-smoke phase; shared policy correction
implemented on 2026-09-06 with CPU unit contracts passing. Real GPU/end-to-end
parity still needs rerunning. This is not a scientific qualification claim.

## Implemented arithmetic-resolution policy

For each species, the absolute uncertainty scale is unity: `FluidState::X` and
the indicator view contain mass fractions, NOT conserved rhoX. Composition
closure is a subtraction from one. With N runtime species,
let u = double epsilon / 2 and gamma = (N+2)u / (1-(N+2)u), modeling composition
accumulation, closure and scaling. The three-point curvature uncertainty is
4*gamma (the absolute weights of the three-point stencil). Subtract this uncertainty from
the absolute second difference, bounded below by zero; keep the original
denominator and refine/derefine thresholds. Every selected species is treated
equally; no stored field is clamped. Other scalar indicators retain their formula.

This model establishes a documented, localized arithmetic-resolution limit; it is not meant to serve as a rigorous mathematical bound across an entire hydrodynamic trajectory. Gradients that are substantially above this scale remain fully detectable, whereas signals below the closure resolution are intentionally not promised to be reliably distinguishable from basic numeric roundoff. Importantly, density-unit rescaling leaves these mesh decisions unchanged. The `test_refinement_indicator_math.cpp` test suite thoroughly covers signed closure noise, 1/7/19/31/200 species configurations, density scales ranging from 1e-20 to 1e20, resolvable trace fractions, and every possible species index. Both the CPU and CUDA paths receive the runtime value of N through the shared view. Because this adjustment meaningfully changes the historical CPU AMR policy, it necessitates reopening the final evidence gates for re-validation.

## Evidence

The cylindrical/spherical species-diffusion development cases select all 19
species, although only the first two have initial physical abundance. Both
backends start with exactly equal fields on the same six-leaf hierarchy.
After one accepted step, before its subsequent dynamic regrid, their species
fields differ only at roughly 1e-15. Closure roundoff in the last, initially
zero species is sufficient to make the existing relative indicator large.

`amr::indicator::loehner_error(0, q, 0)` is approximately `1 / 1.01` for any
nonzero `q` well above the smallest normal double, independent of its absolute
amplitude. The same shared implementation runs on CPU and CUDA. Consequently,
O(1e-16) differences can cross the configured 0.7 refinement threshold even
where the physically populated species do not cross it.

Single-step interior stencils identify the extra coarse-block trigger without
depending on ghost reconstruction: logical x1=1 in the cylindrical CUDA case,
and logical x1=2 in the spherical CPU case. These predict the next-step seven
versus eight leaf counts. Once the two backends evolve on different hierarchies,
their local species fields can differ by O(1e-3); equal global conserved totals
are not sufficient to claim parity.

Local evidence lives in `build/refactor-amr-runtime-smoke-16`, the final-binary
rerun `build/refactor-amr-runtime-smoke-19`, and the four one-step diagnostic
lanes under `build/amr-pre-regrid-diagnostic.Ls1r7A/run`. These are local
development outputs, not portable release evidence.

## Required shared-policy decision and tests

- Define how the indicator should distinguish roundoff from a genuinely small
  physical signal. Consider a documented, field-scale-aware noise model or an
  explicit absolute indicator scale; do not insert a case/network-specific
  epsilon or silently exclude the final species.
- Keep selection, floors/scales, and the estimator in a single shared authority.
  Any changed defaults affect CPU AMR decisions and must be documented as such.
- Test exact-zero fields, signed closure roundoff, real trace-species gradients,
  multiple field units, and both near-zero and large-amplitude stencils.
- Repeat the same-topology single-step CPU/CUDA comparison, then mixed-level
  dynamic refine/derefine, conservative field comparison and restart checks.
- Do not raise validation tolerances or regenerate reference data merely to
  conceal divergent hierarchy decisions.

This issue is independent of the [curvilinear metric/CFL correction](CurvilinearMetricReview.md).
