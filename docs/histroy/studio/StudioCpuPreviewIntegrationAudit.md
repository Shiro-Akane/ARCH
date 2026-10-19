# Local CPU Preview integration audit

The existing Preview readiness is a last-successful-tracked-build contract,
not a proof of complete dependency freshness. It checks profile identity,
tracked inputs and output executable identity and reports the incomplete
freshness boundary in its reason. Do not replace this with a current claim.

## Binding conflict found

- project.ts selects ConfigurationAdapter(preview) whenever PreviewRunner exists.
- ConfigurationAdapter then requires Preview readiness.
- RealInitWorkspace writes one shared buildScope: a successful Preview build ID,
  or selected-binary:SHA when unavailable.
- ConfigurationBridge uses that same scope for schema, inspection and registry.
- Therefore simply binding local CPU profiles would regress independent static
  editing whenever a manifest is absent, changed, or requires a new Build.

The temporary local profile wiring was removed before commit. No Core, cache,
Build Manifest or scientific output was changed in this audit.

## Required next implementation

1. Give static configuration its own binary-bound identity and registry access.
   Missing/stale Preview readiness must not disable schema or inspection.
2. Keep Preview/AMR result identity tied to the successful manifest independently.
   Metadata may be applied only with matching binary, project, case and raw input.
3. Bind the existing Sod/CellularDet profiles to local CPU only after these
   independent identities are propagated through editors and inspectors.
4. Rebuild through Host once profile fingerprint changes; preserve unknown full
   freshness and inspect real warm Preview/AMR behavior.
5. Regression: no manifest, outdated manifest, binary replacement, configuration
   changes, and transition to a new successful manifest; no false Current state.

This audit does not enable Preview, extend model support, or complete desktop UAT.
