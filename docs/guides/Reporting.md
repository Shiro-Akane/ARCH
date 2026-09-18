# Research computing: reliability and issue reporting

Chinese translation: [Reporting.zh-CN.md](Reporting.zh-CN.md) · [Documentation overview](../README.md)

ARCH is scientific simulation software for workstations and high-performance
computing (HPC) environments. Most feedback concerns building the application,
running calculations, reproducing results or using compute resources. This guide
explains how to report those problems and when to contact the maintainer privately
to protect research data or a shared computing environment.

## Build, runtime and numerical questions

Use [GitHub Issues](https://github.com/Shiro-Akane/ARCH/issues) for build errors,
unexpected termination, excessive memory use, restart problems or numerical
results that need investigation. A useful report includes:

- The tested release or commit and the command that produced the problem.
- The operating system, compiler, relevant dependencies and CPU/CUDA configuration;
  for a cluster job, include the resources allocated to that job.
- A small, shareable parameter file or reproducer and the relevant log excerpt.
- The expected and observed behavior. For numerical differences, identify the
  reference, physical time, resolution and comparison method where possible.

You do not need to classify the problem before reporting it. CPU/GPU differences
and failed numerical checks normally belong in this public discussion, provided
the example contains no private data. The [Validation guide](../../validation/README.md)
explains the existing reference problems and acceptance criteria.

## Research data and private reports

Remove access tokens, account details, personal information and unpublished
research data from anything you share, including input files, logs and profiler
captures. Prefer a small synthetic example over a complete research dataset.
If a credential has already been exposed, revoke or replace it; removing it from
a later log does not make it private again.

Problems involving unintended access to files or credentials, unexpected code
execution, or interference with other users' jobs need private coordination.
Use [GitHub's private reporting form](https://github.com/Shiro-Akane/ARCH/security/advisories/new)
when it is available. Otherwise, open an issue asking for a private contact
channel without including sensitive details. Share only the information needed
to reproduce the problem and agree with the maintainer on what can be published.

## Working on shared systems

Run simulations with ordinary user permissions and follow the cluster's job and
resource policies. Custom cases and network-generation recipes contain executable
code; review contributions before building or running them. Use a separate output
directory for each experiment and retain the inputs and software identity needed
to reproduce it. The [build guide](Build.md) describes the existing
memory-monitored build workflow.
