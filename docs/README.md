# ARCH documentation

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

Start here if you are new to ARCH or to computational fluid dynamics (CFD).
CFD represents a fluid on a mesh of small cells and advances quantities such as
density, velocity and energy through time. An ARCH case supplies the initial
state and settings; the program applies the chosen physical and numerical models.

To run your first calculation, follow the [Simulation case guide](guides/SimulationCase.md) from start to finish. Use the [Reference](Reference.md) whenever you need to look up a specific setting, and consult the [Validation](../validation/README.md) section to understand exactly how the calculation's accuracy is verified.

## Start here

- [Build guide](guides/Build.md): dependencies, CPU/CUDA build choices and
  memory-aware compilation.
- [Simulation case guide](guides/SimulationCase.md): build, configure, run, and
  extend a case.
- [Research and API reference](Reference.md): parameter meanings, numerical
  method choices, and the interfaces used to extend the code.
- [CUDA and GPU-AMR guide](CudaBackendStatus.md): supported features, shared
  CPU/CUDA responsibilities, and backend selection.
- [Simulation catalogue](../simulation/README.md): reusable problems and their
  example inputs.
- [Physics notes](physics/README.md): model provenance and the distinction
  between maintained descriptions and historical numerical investigations.
- [Verification and validation](../validation/README.md): what was tested,
  how errors were measured, and how CPU and GPU results compare.
- [EOS runtime tables](../EOS_toolkit/README.md): table layout, provenance, and
  integrity requirements.
- [Legal and provenance index](legal/README.md): project and third-party
  licensing pointers.

## For contributors and reviewers

- [Contributor index](development/README.md): Implementation ownership, code review and testing workflows, as well as the archive of development records.
- [Tests](../tests/README.md) and [tools](../tools/README.md): focused regression
  checks and shared build, generation, and validation utilities.
- [Validation index](../validation/README.md): the combined acceptance decision;
  module summaries explain scientific methods and link the recorded evidence.

## Directory contract

- [guides/](guides/README.md) contains task-oriented workflows.
- `physics/` provides physics model notes and explicitly labels historical numerical investigations.
- `development/` maintains contributor decisions, conventions, and execution records.
- `legal/` serves as an index; the canonical license and notice files are kept at the repository root for compatibility with standard tooling.
- `validation/` is located outside of `docs/` because its records substantiate explicit pass/fail claims, and it independently manages its own immutable inputs, metrics, and generated figures.
- Source and simulation directories house the actual implementations and reusable example inputs, supplemented only by the brief local contracts needed immediately next to the code.

Add new documents to this index and link them from the owning module when
appropriate. Do not create a second general documentation or validation tree.
