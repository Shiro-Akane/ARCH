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
release capacity only after child close. Live HTTP disconnect is verified while an exactly-owned reader worker is
explicitly SIGSTOP-stalled; metadata and slice both reap and recover. This does
not claim natural HDF5 instruction timing or native desktop-close coverage.

One metadata or slice worker at a time;15s maximum lifetime,64KiB stdout budget,
256MiB Node heap cap. Heap cap is not a hard RSS/WASM cap.
Errors:400 invalid selection/file identity,409 BUSY or changed session,
413 OUTPUT_LIMIT,504 TIMEOUT,422 worker/read response failure.
A disconnected client receives no successful late result.
No endpoint executes Core, writes a file, starts simulation, or changes Config.
Formal completion/provenance/native geometry publication remains a Core IO
contract prerequisite for the final scientific Viewer.


## Candidate global display overview (Linux only)

POST /api/plotfile/audit-overview uses the existing Origin/protocol headers and owned worker:

    {"projectId":"session","relativePath":"results/plt_0000.h5",
     "expectedFileSha256":"<64 lowercase hex>",
     "overview":{"field":"DENS","width":32,"height":24}}

Only published candidate NativeGrid Cartesian 1D/2D is accepted. Axes are 1..32 pixels;
1D height=1. Existing 64 MiB file/64 KiB worker-output limits remain.
Response audit-overview-1 retains observed identity/unknown scientific certification.
Overview values are coordinate overlap weighted DISPLAY means in x1-fastest [height,width],
not native values or cell-measure integrals. Nonfinite contribution/empty/overflow masks to null.
representativeIndices select the largest-overlap raw native cell (first encounter ties).
Inspector fetches that index through audit-slice with the same digest. Do not label the mean
as a native value at the mouse position.

All leaf geometry and selected fields are scanned; the complete file is hashed.
Fixed pixels bound response, not first-overview scan cost. Zoom/pan redraw existing LOD.
Viewport refinement and native hierarchy outline contracts are not implemented yet.


### Native leaf outlines in overview

Optional overview.nativeBlocks (candidate-leaf-outlines-1) contains only stored-active-leaf,
file-local records. Records include index, firstCellIndex, level, logicalKey/coordinates,
native cell envelope lower/upper and x1-first no-ghost cellShape. No parent/coarse records
or fields are synthesized. totalBlocks is the full stored count; at most128 records are
returned. complete=false means limited outlines, while the field scan still covers all cells.
Clients must validate shape/count/index/key/extent consistency and associate records only
with the enclosing observed file digest. Level visibility is display-only and filters
outlines, not field contributions or scientific data.


### Exact stored native point

POST /api/plotfile/audit-point uses the same protected origin/protocol and file identity:

    {"projectId":"session","relativePath":"results/plt_0000.h5",
     "expectedFileSha256":"<64 lowercase hex>",
     "pointQuery":{"field":"DENS","point":[0.5,6.5]}}

Only candidate Cartesian native 1D/2D; point length equals dimension.
Cell bounds are half-open, with the full-domain maximum included.
One exact match returns audit-point-1, a single raw slice plus pointEvidence.
No interpolation, nearest-cell or guessed-level fallback. NO_NATIVE_CELL and
AMBIGUOUS_NATIVE_CELL fail; current worker-error mapping is HTTP422.
Inspector validates the echoed request/coverage/boundary rule and the returned
cell bounds. Each point read currently scans bounds and hashes the full file;
fixed response size does not imply indexed or cheap reads.
