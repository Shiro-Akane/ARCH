# ARCH documentation

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

This directory is the canonical entry point for maintained documentation.

## Start here

- [Simulation case guide](guides/SimulationCase.md): build, configure, run, and
  extend a case.
- [Research and API reference](Reference.md): parameters, policies, and
  extension contracts.
- [Physics notes](physics/): implementation boundaries, provenance, and
  numerical-alignment records.
- [Verification and validation](../validation/README.md): executable claims,
  acceptance criteria, metrics, and backend parity.
- [EOS runtime tables](../EOS_toolkit/README.md): table layout, provenance, and
  integrity requirements.
- [Legal and provenance index](legal/README.md): project and third-party
  licensing pointers.

## Directory contract

- `guides/` contains task-oriented workflows.
- `physics/` contains long-lived design, provenance, and mathematical notes.
- `legal/` is a discovery index; canonical license and notice files remain at
  the repository root for standard tooling.
- `validation/` remains outside `docs/` because its records support explicit
  pass/fail claims and own their immutable inputs, metrics, and figures.
- Source and simulation directories contain implementation and reusable example
  inputs, plus only short local contracts needed beside the code.

Add new documents to this index and link them from the owning module when
appropriate. Do not create a second general documentation or validation tree.
