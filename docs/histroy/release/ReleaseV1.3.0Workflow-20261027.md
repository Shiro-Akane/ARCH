# Archived v1.3.0 publication workflow

Archived 2026-10-27 from the workflow guide at `dd42d4fa4`. The [original workflow](ReleaseV1.3.0.workflow.yml) is preserved byte-for-byte as inert history; its former authorization does not apply to v1.4.0.

## v1.3.0 source publication

[release-v1.3.0.yml](ReleaseV1.3.0.workflow.yml) is a one-shot, explicitly authorized
release from `compute/optim`, triggered only when that workflow file changes.
It waits for the exact head's PR and push CI, all three default-setup CodeQL
analyses and the original PR comparison gate; missing, skipped, cancelled or
failed results cannot unlock it. The original candidate and note digests must
match. Existing releases/tags are never replaced.

The isolated publication job uses GitHub's temporary repository token to update
PR #12 and call the normal merge API with the accepted head SHA. It has no
admin/bypass operation and does not alter branch rules. Ordinary public PR CI
remains read-only. After merging, the entire tracked file tree must exactly
match the scanned head. The workflow explicitly dispatches the existing full
CI on the actual main commit, since token-generated pushes do not trigger
other workflows. Missing dispatch-base information conservatively selects
complete CPU and Studio work.

Only successful main CI allows the annotated `v1.3.0` tag and formal source
release. Security qualification explicitly reuses the accepted identical file
tree; it does not invent a new main scan. Notes use tag-pinned English/Chinese
guide links and the existing GUI screenshot, with actual CI/security identities
appended. No prebuilt scientific binaries or raw simulation outputs are assets.
A failure stops the current stage; no tag is moved and no rule is weakened.
See [GitHub token behavior](https://docs.github.com/en/actions/tutorials/authenticate-with-github_token)
and [release command](https://cli.github.com/manual/gh_release_create).
