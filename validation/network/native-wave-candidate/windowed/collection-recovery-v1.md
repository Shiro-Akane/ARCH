# Window factory collection recovery — 2026-09-17

Both fresh factories completed all eleven commands successfully. The original
build worker exited 0; no build or scientific test is rerun for this recovery.
The first separately uploaded collector failed before creating archive output:

```text
collect_window_factory_v1.py -> validate() -> subprocess.run(['ninja', '-t', 'commands', target])
FileNotFoundError: [Errno 2] No such file or directory: 'ninja'
```

The build worker had added the existing toolchain directory to PATH, whereas
the collection SSH environment did not. The frozen CMake cache explicitly gives
`CMAKE_MAKE_PROGRAM:FILEPATH=/home/ubuntu/projects/.envs/arch/bin/ninja`.
The revised collector verifies that cache's recorded SHA before taking its
absolute Ninja path. It still only asks Ninja for the original command recipes;
it does not compile, relink, change any dependency or relax any validation.

The failed v1 collector remains on the server. The revised source is uploaded
separately as `collect_window_factory_v2.py`; both it and this recovery note are
included with the first successful archive. Runtime qualification remains false.
Two added local preparation tests cover the original absolute path and rejection
of missing, duplicate, relative or non-Ninja settings; these are not build proof.
