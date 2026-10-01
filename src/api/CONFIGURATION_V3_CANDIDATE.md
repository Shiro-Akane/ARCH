# Configuration extension v3 — candidate contract

Status: proposed wire contract, NOT implemented by the current executable.
The implemented v2 reference remains CONFIGURATION_API.md. Required/default
classification remains owned by ConfigurationContractPlan.zh-CN.md; this
document defines transport, not another scientific parameter catalogue.

## Envelope and compatibility

Keep schemaVersion "1.0". configuration-schema and configuration-inspection
use version "3"; capabilities.extensions.configuration must advertise "3".
Every embedded configuration result carries its own version. Unsupported
versions fail with an actionable incompatibility message and keep editor text.
Do not translate v3 missing values into v2 fallback values.

Existing commands, stdin UTF-8 limits (1 MiB), response limit (8 MiB including
newline), stdout JSON/stderr logs, and exit categories remain. Success exits 0;
request errors 2; invalid/incomplete configuration 3; response overflow 7.
A bounded error is a complete JSON envelope, never a prefix of a large reply.
Preserve confirmed request identity when possible, set coverage flags false
for omitted data, and include RESPONSE_TOO_LARGE; never label truncation ok.

## Schema parameter

Retain key/type/group/presentation/options/path/units/constraints. Add:

- requirement: {kind: "required"|"conditional"|"optional", condition: Condition}.
- applicability: Condition. Applicability and requirement are independent.
- allowedDefault: null or {value: scalar, source: "documented-default",
  evidence: string}. A legacy fallback is not sufficient evidence.
- templateRecommendations: [{value: scalar, evidence: string}]. These are
  opt-in edits; inspection and runtime must not apply them as defaults.
- usage: "simulation"|"verification"; caseId is null for standard parameters
  and an exact registered model ID for case declarations.
- defaultValue/defaultSource are removed in v3 to prevent accidental fallback.

Condition is {id: string, dependencies: string[], description: string}.
IDs identify Core-owned registered predicates, not frontend executable
expressions. Core evaluates them; clients must not infer conditions from
labels or dimensional guesses. Schema publishing does not evaluate a config.

Options retain canonical value and acceptedNames. Unknown methods are errors,
not a substitute selection. Units preserve known/dimensionless/not-applicable/
coordinate-dependent/mixed-state/not-specified distinction, including null unit.

## Inspection parameter

Each returned parameter contains:

- key, caseId, type, group, usage.
- inputState: "missing"|"present"|"invalid"|"duplicate".
- rawValue: string|null; locations: SourceLocation[].
- parsedValue: scalar|null: strict explicit token conversion ONLY.
- resolvedValue: scalar|null: pre-Setup configuration resolution.
- valueSource: "input"|"case-defined"|"derived"|"documented-default"|null.
- sourceEvidence: null or {owner:string, dependencies:string[]}.
- valueStage: "configuration-resolution-before-setup".
- requirement: {conditionId:string, state:"satisfied"|"not-applicable"|
  "unknown-dependency", required:boolean|null, missingDependencies:string[]}.
- applicability: {conditionId:string, state: same enum,
  missingDependencies:string[]}.
- units: the schema unit descriptor; no client conversion.

A missing token has parsedValue/rawValue null and locations [] even if a
registered default/model declaration supplies resolvedValue. Explicit false,
zero and empty string are real inputs, not absence. An invalid token remains
raw, with both values and source null; no rescue default. Duplicate entries
have rawValue null, parsedValue/resolvedValue null, source null, and all
occurrences in locations (each has rawValue). Never pick first/last silently.
A type-valid but range/combination-invalid token retains parsedValue for
display but has resolvedValue/source null and a diagnostic.

SourceLocation is {source:"stdin"|string, line:positive integer,
column:positive integer, endColumn:positive integer, rawValue:string|null}.
Columns count UTF-8 bytes, 1-based and end-exclusive; full-line malformed input
uses its physical line span. Missing keys have no invented source position.
rawValue is the exact value span after existing comment stripping, before
trim; identity hashes all original input bytes, including whitespace/comments.

Registered case-defined/derived values require authoritative declarations;
an observed Get(key, default) call does not authorize a physical default.
Derived dependencies must already be valid. Inspection does not execute Setup
to discover declarations or defaults. After Setup changes, runtime must
revalidate before publishing a complete immutable run configuration.

## Completeness, execution and diagnostics

identity retains caseId/requestId/configRevision (SHA-256 of exact UTF-8 input).
Host independently binds project/source/binary/build/session/editor revision.

coverage contains standardParametersComplete, auxiliaryParametersComplete, caseParametersComplete,
conditionsComplete and diagnosticsComplete booleans. An error response may
contain partial records, but must mark the corresponding coverage false.
Completeness is {state:"complete"|"incomplete"|"invalid"|"undetermined",
scope:"declared-configuration-before-setup"}. "complete" requires all applicable
declared requirements resolved and all applicable checks evaluated. It is
never equivalent to simulation readiness.

execution retains setup=not_executed, eos=not_loaded, cuda=not_initialized,
filesystem=not_accessed, simulationReadiness=not_checked. caseRegistration and
caseDeclarations say checked/not_checked separately. static validationStage is
"syntax"|"typed-input"|"conditional-resolution"; later run validation is separate.

Each diagnostic has code, severity, parameterKey:string|null, module:string|null,
conditionId:string|null, message, expected:{type:string|null,units:object|null},
locations:SourceLocation[], relatedKeys:string[]. Stable codes include existing
INVALID_INTEGER/INVALID_NUMBER/INVALID_BOOLEAN/INVALID_EXPRESSION/INVALID_OPTION/
INVALID_RANGE/RETIRED_PARAMETER plus MALFORMED_LINE, EMPTY_KEY, DUPLICATE_PARAMETER,
MISSING_PARAMETER, UNKNOWN_PARAMETER, UNRESOLVED_DEPENDENCY.
Do not report a dependent requirement as missing while its condition is unknown.
Collect all currently decidable errors; unknown case/species membership must
not be guessed. A global syntax error is not assigned a fabricated key.

Configuration validity and draft persistence are independent. Save preserves
raw invalid/incomplete text under the existing ownership and conflict checks.
Save does not grant Run/Restart/Preview eligibility. Explicit forbidden keys
remain visible with errors until an explicit Undo-able remove.

## Shared candidate fixtures and implementation gates

examples/configuration-v3-candidate contains hand-authored protocol expectations,
not captured ARCH outputs. Core and Host must consume the same files; no copied
frontend expectation table. The initial syntax fixture deliberately reports
partial coverage. It preserves valid explicit zero/false alongside malformed
and duplicate inputs, proving error envelopes are still useful to an editor.

Before enabling v3, complete actual-response regression for: valid declared case, empty input, missing switches, each required
class, documented defaults, case-defined/derived values, inactive explicit bad
tokens, retired/unknown keys, unknown methods, missing path vs unchecked path,
raw whitespace identity, unknown case, response limit and stale Host identity.
The candidate corpus is not implementation acceptance; these cases must pass
against the migrated Core and Host before enabling v3.

Then migrate Core construction/CLI/inspection/case declarations together,
followed by Host runtime validators/types and Studio. Preserve shared scientific
math, strict floating point and existing authoritative constants. Do not enable
JENS/RZ or claim full model Preview from this configuration change.

## Declaration scope and derived summaries

The schema's parameters array is the standard catalogue. auxiliaryParameters
declares shared nonstandard input such as log_dir; caseDeclarations contains
(caseId, source, sourceSha256, parameters). caseDeclarationsComplete says whether
all registered models are represented, not whether one selected case is complete.
The candidate includes Sod only; implementation must register the other models
before claiming their declaration coverage. All seven editable Sod primitive
inputs are required; old Get fallback arguments are not published as defaults.

Inspection parameters flatten the selected declarations with caseId retained.
Auxiliary coverage is separate. log_dir may derive from resolved out_dir as in
the existing CLI; it is not a physical default. The optional resolved summary
contains named values with valueSource and sourceEvidence, e.g. dimension from
the three explicit block counts. These summaries are not new editable keys.

The schema is query-only. Inspection evaluates formal-evolution requirements
by default; an explicit init-only caller uses the same resolver with initial-state
purpose, which can omit tmax. This internal purpose does not add a legacy-default
mode or permit ordinary runs to omit their endpoint.
