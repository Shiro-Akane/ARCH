# Phase 1C.2 UAT candidate

Branch: studio/phase1c2-uat-fixes. Base tag studio-phase1c1-v0.4.1, commit 5dd195138afca5372d79ab9c9aab7689854d6b8a.
Implementation reached UATF-M11. Final checkpoint M12 is deliberately pending human acceptance.

## Issue disposition (implementation, not human sign-off)

| Issue | Disposition | Change |
| --- | --- | --- |
| 01 | Fixed | Four mode/input summaries |
| 02 | Fixed | Centered empty view / Generate |
| 03 | Fixed | Custom Plotfile picker |
| 04 | Fixed | Shared dark Select styling |
| 05 | Fixed | Removed unmodeled Mock fields |
| 06 | Fixed | Dark outer plot background, readable H5Web axes; values/colormap untouched |
| 07 | Fixed | Non-clickable brand |
| 08 | Fixed | 1D Profile explanation |
| 09 | Fixed | Processing text in collapsed provenance |
| 10 | Fixed | Symmetric radial Mock Temperature |
| 11 | Fixed | Explicit Mock pressure step label |
| 12 | Fixed | Hotspot intensity label, raw key retained |
| 13 | Fixed | Mock X/Y range plus exact input |
| 14 | Fixed | Contextual Generate/Update/Retry |
| 15 | Fixed | Scoped readable typography |
| 16 | Fixed | Stable renderer key and retained prior preview; explicit generation |
| 17 | Fixed | Mock/Real edit history with keyboard Undo/Redo and drag transactions |
| 18 | Fixed | Real parameter Inspector, no Mock point Inspector in Config mode |
| 19 | Fixed | Compact sticky block navigation |
| 20 | Fixed | Numeric horizontal scrub, fine adjustment, cancel, pointer capture |
| Browser context menu | Accepted | Native browser behavior; no interception |
| Paper plot / crossfade | Deferred | Optional polish; dark default and stable rendering prioritized |
| Save As disk delivery | Pending | Must be performed and confirmed by human on Windows |

## Evidence

47 automated tests and lint/typecheck/build pass. Added history, scrub, radial temperature, mode requirements and rendered Inspector/control tests. Existing parser/export/custom preservation/Plotfile tests pass. Browser checks include numeric scrub + single Undo, exact edits/redo, real Inspector and PRES LineVis, and desktop snapshots. Before screenshots remain in earlier task conversation; after snapshots captured during this task. No new screenshot baseline execution or claims of human approval.

No parser grammar, serialization contract, ARCH Core, dependency or lockfile changes. No ARCH run/build. Candidate files are not yet committed or tagged. Complete MANUAL_UAT_FIX_REPORT.md and final checkpoint only after human UAT, including Windows file existence, contents and unchanged original evidence.
