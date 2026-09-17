# Canonical-path archive recovery — 2026-09-17

The completed factory build is unchanged; no compilation or physics was rerun.
The absolute-Ninja collector passed the actual build audit, but the independent
local verifier rejected its first archive with:

```text
ValueError: archive must contain only regular files/directories, no links or special nodes
```

NVCC dependency output contains legitimate `../` aliases of private headers.
Adding both the aliases and canonical files produced duplicate inode entries;
GNU tar emitted hard links and stripped path prefixes. This is an archive
construction error, not a failed build, and is not accepted as a verified backup.

The old raw (42,264,601 bytes, SHA-256
`97c1d8e185b6b7976ad6ad9fce88c43935fd2455182c47bd70e38758f8c115fa`)
and compact (138,877 bytes, SHA-256
`d7ee9adf60b0facaa54e2988477c21c2cd28aff231d28a7bf8da7e20e351fc77`)
archives and collection receipt remain untouched on both server and local
download storage. No successful local receipt exists for that version.

The new collector retains original dependency strings/hashes in the build record,
but deduplicates archive members by their resolved, in-scope regular-file paths.
Symlink/out-of-scope checks and the independent strict verifier remain unchanged.
It writes NEW v2 archive paths, and retains the rejected v1 archive bytes as opaque
raw-only evidence. Do not treat or extract those nested rejected archives as a
qualified source tree. The new actual source files are separate canonical members.

The still-undeployed focused input gets a new v2 package solely to require the
accepted v2 archive/local receipt. Scientific settings, the original numerical
validator, factory objects and executables are unchanged. The unused v1 input
package is retained. The focused collector gets the same canonical-path fix.
