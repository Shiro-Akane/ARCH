# Targeted Fix & Regression Report

## Baseline
- Active target: `01_TARGETED_FIX_REGRESSION_v2.md`; context handoff is historical context only.
- Original workspace: `/home/arch/projects/ARCH-linux`, branch `studio/phase1c2-uat-fixes`, HEAD `5dd195138afca5372d79ab9c9aab7689854d6b8a`, latest relevant release `studio-phase1c1-v0.4.1`; dirty with existing broad UAT work. No v0.4.2 release was assumed.
- Original working changes were preserved untouched. An independent worktree `/home/arch/projects/ARCH-preview-targeted`, branch `studio/preview-targeted-fixes`, preserves that UAT state in non-release baseline commit `ee63416e20c59f0082eec5958b1d9f492f9f218d`.
- This report belongs to the subsequent focused commit `fix(studio): preserve preview state and refine update workflow`; resolve its hash with `git log -1 --format=%H -- studio/TARGETED_FIX_REGRESSION_REPORT.md`.
- Before the focused commit, only the six targeted UI files, two regression tests in one new file, this report and studio STATUS are changed. No ARCH Core change relative to the release baseline.

## Fixed
- Preview retention: keep the same Preview mounted for Mock and Real Config. Source changes and actual file selection invalidate pending generation and mark retained data stale. Existing reducer/data architecture is unchanged.
- Provenance: Real Config explicitly displays `Previous preview — not generated from current config`; point inspection cannot attribute Mock data to Real Config. Empty means no successful in-session preview exists.
- Canvas centering: CSS grid status layer fills the actual canvas; H5Web's public `Html overflowCanvas` mounts status into its canvas area. Toolbar and sidebars are excluded. No positioning offsets were introduced.
- Update action: primary Update Preview sits immediately below the Hotspot group, applies to the entire Mock config and is disabled during generation/invalid input. Existing bottom action remains. No live preview or new shortcut.

## Reproduction verification
| Workflow / fixture | Before | Verified after |
| --- | --- | --- |
| Hotspot Mock generate, numeric X 0.5 -> 0.7 | Subsequent config switch removed image | Edit retains image as stale; explicit update moves hotspot and returns current |
| Mock -> Real Config -> Open existing `sod.par` -> Mock | Real Config replaced Preview with unrelated empty context | Image remains; explicit previous-config provenance; return/update replaces prior data |
| First empty / generating / stale | Constrained empty block and top-offset status | Layout centered within actual canvas at desktop sizes below |
| Edit Hotspot slider -> update | Only practical update near far header | Slider X 0.7 -> 0.259 updates numeric value, retains old image without generating; nearby action performs explicit update |

Structural DOM measurements (center delta in CSS px; no document horizontal overflow):
- Empty: 1280x720, default 1686x1272, 2560x1440; |dx| < 0.01, |dy| < 0.51 (border/subpixel rounding), relative to `.mock-viewport`, below toolbar.
- Stale/previous: 1280x720, 1920x1080, 2560x1440; |dx| < 0.01, dy = 0, direct parent H5Web canvas area.
- Generating with prior image: 1280x720, 1920x1080, 2560x1440; dx = dy = 0 in canvas area. First generation also checked at wide desktop.

## Regression verification
- Browser tested the production build at `http://127.0.0.1:4175/`.
- Mode explanations, Mock/Real labels, dark plot and Viridis scale retained. Density -> Temperature updates field/range. Existing Mock intensity label retained.
- Mock point selection: x=0.702148, y=0.500977; density=1.99981, temperature=0.999845, pressure=0.200000. Stale clears selection without clearing image.
- Scroll zoom changes axes; subsequent drag changes plotted SVG geometry; reset available. No overlay interception of canvas navigation.
- Existing real `sod.par` opens, Grid metadata and contextual Parameter Inspector show actual keys/source lines; raw source remains available.
- Real nblockx1 numeric 4 -> 8 -> Undo 4 -> Redo 8 -> Revert 4. Numeric scrub 4 -> 6 -> one Undo 4. Custom x_pos edit to 0.6 then Revert works.
- Save As still reports `Save As requested`; unchanged export tests verify exact no-edit text, minimal edited diff and original preservation. This is not a claim of human-confirmed Windows disk persistence.
- Existing `sod.par` used as invalid Plotfile: readable HDF5 error; opening existing `sod-1d.h5` afterwards recovers. No new scientific fixtures generated.
- Real Plotfile enumerates DENS, ENER, PRES, VELX from file; PRES displays 64-sample 1D Profile, min 0.1/max 1, sample 33 x=0.5078125/value=0.3054751636143017.
- Desktop sidebars remain accessible and aligned at tested sizes. No mobile scope expansion.

## Automated checks
- `npm test`: **49/49 pass**, zero failures/skips. Includes original 47 checks plus two focused retention/cancellation tests. No existing test weakened.
- `npm run lint`: pass, zero warnings.
- `npm run typecheck`: pass.
- `npm run build`: pass; production output generated without committing dist.
- `git diff --check`: pass.
- Expected HDF5 diagnostic is from negative tests. Existing Vite large-chunk advisory remains; no performance refactor added.

## Remaining issues
- No remaining failure in the three targeted fixes or exercised regression paths.
- Historical broad UAT/human Windows Save As acceptance remains separate and is not reclassified as passed by this report. This round does not reopen it.
- No push/tag, Core/solver/parser/scientific changes, new ARCH execution, or Phase 2 work.
