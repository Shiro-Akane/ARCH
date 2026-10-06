# Phase 2G — initial AMR closure

The authoritative B acceptance record is PHASE2G_B_AMR_COMPLETION_REPORT.md.
Immutable checkpoint: studio-phase2g-b-v0.15.0,
2cd4dbbabc6a17123950a4b2ab6d612a6c0831b2.

B delivered selected-binary case discovery and 11-model initialization inspection,
AMR parameters/resource estimates, real Sod1D and CellularDet2D initial hierarchies,
complete/limited/no-snapshot handling, block geometry Inspector and identity-safe overlay.
Its real scientific/UAT coverage remains in the B report.

C reuses these workflows in the packaged desktop. Native Windows UAT generated a
Complete Sod hierarchy and matching field/config/build/EOS overlay.
Registered case does not imply field support; limited is not complete; estimates
are not OOM guarantees; Init samples are not AMR cell values. Display transforms
redraw retained hierarchy without new AMR requests. No additional AMR science in C.
