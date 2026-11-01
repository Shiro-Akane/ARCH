# Repository maintenance

[CODEOWNERS](CODEOWNERS) assigns review ownership to `@Shiro-Akane`. Configure required reviews and the **CI required** status check in the `main` branch ruleset.

Contributor responsibilities are described in the [development guide](../docs/development/README.md). Build and numerical feedback, data protection, and private coordination for security-sensitive problems are covered by the [reporting guide](../docs/guides/Reporting.md).

## Continuous integration

[ARCH CI](workflows/ci.yml) runs Tooling, the CPU Release suite with KLU, and Studio/Host checks on disposable GitHub-hosted Ubuntu machines. It runs for pull requests targeting `main` and pushes to `main` or `compute/optim`. The [workflow guide](workflows/README.md) explains coverage, change selection, resources and diagnostic artifacts.

CUDA execution, native Windows tests and desktop packaging require their own environments. Record those results with the tested source, toolchain and hardware when a change affects them.

## Dependency updates

Review Dependabot changes against the manifest, lockfile and affected runtime. Run the relevant tests and a current dependency audit; update [Studio dependency notices](../studio/DEPENDENCIES.md) and the locked license inventory when necessary. Evaluate major upgrades separately from routine patches.

For Electron updates, keep the manifest, lockfile, installed runtime and packaged runtime in agreement. The shared version validator checks these inputs. Rebuild native desktop packages and test their windows: hosted CI skips Electron downloads and cannot exercise the packaged runtime.

Keep release notes in the [release directory](../docs/releases/README.md), with the version's actual changes and validation scope. Remove merged dependency branches after integration.
