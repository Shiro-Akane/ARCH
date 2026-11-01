# Comments and documentation

Write for the person using or maintaining the software. Explain the behavior, the reason for a choice and the next practical step in plain language.

## Choose the reader

| Reader | Content |
| --- | --- |
| New user | A short path from installation to a useful result, with runnable examples |
| Research user | Units, assumptions, compatible methods, diagnostic meaning and numerical limits |
| Contributor | Implementation ownership, data flow, invariants and relevant tests |
| Reader of validation results | Problem, reference, configuration, measured error and interpretation |

Keep build commands in the build guide and daily Studio operation in its illustrated guide. Other pages should link to these instructions instead of maintaining another copy.

## Explain code where it is used

Comments should cover units, coordinate conventions, indexing, boundary conditions, non-obvious constants and resource lifetimes. Explain mathematical conventions beside their implementation and cite the relevant method or data source. A file introduction can describe its responsibility and the order of its main operations.

Avoid comments that repeat an expression, narrate an editing session or list completed tasks. Use stable module names rather than temporary project phases or personal work directories. Choose file boundaries by responsibility and data lifetime.

## Write current documentation

Begin with what the reader can do. Put requirements before commands and explain important output after them. Show Linux and Windows instructions together when they perform the same task. Use ordinary example paths and state how relative paths are resolved.

For UI instructions, pair actual screenshots with a few numbered controls or short captions. Keep field names, buttons, units and sample paths legible. Label an initial-state preview and an evolved result accurately. Supply meaningful alternative text.

Describe limitations where they affect a choice. Prefer a concrete statement such as “Plotfile overviews support Cartesian 1D/2D and cylindrical r-z” to repeated general disclaimers. Keep approval numbers, intermediate plans, conversation transcripts and troubleshooting-session narratives out of published guidance.

## Preserve technical meaning

Documentation changes must preserve formulas, symbols, units, numerical thresholds and source attribution. A translation should carry the same commands, parameter meanings and scope. State which executable, inputs and environment produced a validation result; distinguish numerical errors from timing measurements.

Use the [documentation checker](../../tools/check_documentation.py) to validate local destinations and heading anchors. Review examples and screenshots as well: a valid link alone does not establish that a command or explanation is correct.
