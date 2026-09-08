# Repository maintenance files

[CODEOWNERS](CODEOWNERS) identifies `@Shiro-Akane` as the default code owner for
review requests. It covers this directory as well as the rest of the repository.
Requiring owner approval is a separate GitHub branch-rule setting; this file
does not enable that setting or grant repository access. See
[GitHub's code-owner documentation](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/about-code-owners).

The security reporting instructions live at the repository root in
[SECURITY.md](../SECURITY.md) and [SECURITY.zh-CN.md](../SECURITY.zh-CN.md).
The [contributor guide](../docs/development/README.md) explains implementation
ownership and review expectations. These policy files do not enable repository
security settings.

## Continuous integration

[ARCH CI](workflows/ci.yml) runs the existing tooling suite and a CPU Release
build with KLU on GitHub-hosted Ubuntu machines. It is triggered by pull requests
targeting `main` or `CUDA_complete_v1`, pushes to those branches, and manual runs.
The [workflow guide](workflows/README.md) describes coverage, resource limits,
artifacts and the maintainer setup. CUDA execution remains a separate GPU
validation activity; the CPU CI result is not a GPU validation badge.

After the workflow has produced its first successful run, maintainers can add
the `CI required` check from GitHub Actions to the `main` ruleset's required
status checks. This is separate from the pull-request and reviewer settings.
Workflow changes still follow the repository's normal pull-request process.
