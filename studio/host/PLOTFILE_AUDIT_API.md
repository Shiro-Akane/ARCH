# Local Plotfile audit API

These loopback-only, read-only operations expose observed file structure and bounded
raw samples. They are not a scientific result certification or a formal Viewer.
Use the existing exact Origin, X-ARCH-Studio, protocol and desktop ownership headers.
Both routes require POST application/json; the existing 1 MiB/10 s request-body
budget applies. Browser program/args/cwd/env fields are rejected.

## Metadata

POST /api/plotfile/audit-metadata

    {"projectId":"current-session-id","relativePath":"results/plt_0000.h5"}

Response: protocolVersion, projectId, relativePath and metadata. The metadata
includes exact file SHA/device/inode/size/mtime/ctime, blocks/cellShape and fields.
Completion remains unknown, units/scientific identity unavailable and
renderEligible=false. Readable HDF5 and a stable hash do not prove writer completion.

## Raw slice

POST /api/plotfile/audit-slice

    {
      "projectId":"current-session-id",
      "relativePath":"results/plt_0000.h5",
      "expectedFileSha256":"<SHA-256 from metadata>",
      "slice":{"field":"PRES","block":2,"start":[0],"count":[1]}
    }

Response: protocolVersion, projectId, relativePath and result.
result.payload contains field, block, start, shape, x1-fastest order,
original global linearIndices, values and aligned stored Cartesian cell centers.
start/count follow stored cellShape order: [x] / [y,x] / [z,y,x].
Select one block, at most512 samples; no stride/LOD aggregation is provided.
Float32/float64 raw values are retained; NaN/Infinity/-Infinity use explicit strings
with a diagnostic, not null, zero, abs or epsilon substitutions.
Stored Cartesian centers are not native curved-cell bounds/volumes.
Unknown units remain null; do not label them CGS by guessing.

The existing fixture has four blocks of16 cells: global sample32 corresponds to
block2/start0, not block0/start32. Consumers must use actual metadata shape.

## Identity, cancellation and errors

Project mismatch, traversal, absolute paths, symlinks and stale expected file SHA
reject. Selection is copied before await; the selected path must still match the
pinned file at read completion. A changed Project session rejects a late response.
Client disconnect aborts its reader signal; worker timeout/cancel/output limit
release capacity only after child close. HTTP disconnect timing coverage is still
pending; fixed-worker cancellation/exit behavior is directly tested.

One metadata or slice worker at a time;15s maximum lifetime,64KiB stdout budget,
256MiB Node heap cap. Heap cap is not a hard RSS/WASM cap.
Errors:400 invalid selection/file identity,409 BUSY or changed session,
413 OUTPUT_LIMIT,504 TIMEOUT,422 worker/read response failure.
A disconnected client receives no successful late result.
No endpoint executes Core, writes a file, starts simulation, or changes Config.
Formal completion/provenance/native geometry publication remains a Core IO
contract prerequisite for the final scientific Viewer.
