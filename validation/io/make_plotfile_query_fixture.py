#!/usr/bin/env python3
"""Bounded synthetic storage workload, not a Core/AMR scientific reference.

Writes new local evidence only. No solver, checkpoint, or existing-file rewrite.
Requires numpy/h5py; use the local validation environment.
"""
import argparse
import json
from pathlib import Path

import h5py
import numpy as np


def write_fixture(directory: Path, blocks: int) -> dict:
    nx, ny = 64, 32
    shape = (blocks, ny, nx)
    cells = blocks * ny * nx
    directory.mkdir()
    output = directory / 'output'
    output.mkdir()
    path = output / 'reference_HLLC_plt_0000.h5'
    # Tiled Cartesian leaves, x1 fastest. Synthetic values equal linear indices.
    index = np.arange(cells, dtype=np.float64)
    b = np.arange(cells, dtype=np.int64) // (nx * ny)
    i = np.arange(cells, dtype=np.int64) % nx
    j = (np.arange(cells, dtype=np.int64) // nx) % ny
    lower_x = (b * nx + i).astype(np.float64)
    lower_y = j.astype(np.float64)
    zero = np.zeros(cells, dtype=np.float64)
    with h5py.File(path, 'x') as f:
        f.attrs.update(time=0.0, dim=2, geometry='cartesian', time_unit='s',
                       plot_publication_version='candidate-1',
                       plot_publication_state='complete',
                       plot_publication_method='checked-close-atomic-replace',
                       plot_storage_order='x1-fastest', plot_identity_state='unknown',
                       synthetic_fixture='query-workload-1')
        grid = f.create_group('Grid')
        grid.attrs.update(coordinate_unit='cm', coordinate_basis='cartesian')
        for key, value in [('x', lower_x + .5), ('y', lower_y + .5), ('z', zero)]:
            grid.create_dataset(key, data=value)
        grid.create_dataset('level', data=np.zeros(blocks, dtype=np.int32))
        grid.create_dataset('morton', data=np.arange(blocks, dtype=np.uint64))
        native = f.create_group('NativeGrid')
        native.attrs.update(version='candidate-cartesian-1', centering='cell',
                            ghost_cells=0, block_kind='active-leaf',
                            center_basis='cartesian', measure_source='GridMetrics::CellVolume',
                            measure_convention='active-coordinate-product; inactive-measures-omitted',
                            measure_unit='cm^2', measure_normalization='per_unit_transverse_length',
                            logical_identity='file-local level/logical_x1/logical_x2/logical_x3')
        # Candidate metadata is copied solely to exercise the reader. It does NOT
        # certify this synthetic h5py file as a production GridMetrics publication.
        for axis, low, high in [(1, lower_x, lower_x + 1), (2, lower_y, lower_y + 1), (3, zero, zero)]:
            native.create_dataset(f'x{axis}_lower', data=low)
            native.create_dataset(f'x{axis}_upper', data=high)
            native.create_dataset(f'logical_x{axis}', data=np.arange(blocks, dtype=np.uint32) if axis == 1 else np.zeros(blocks, dtype=np.uint32))
        native.create_dataset('cell_measure', data=np.ones(cells, dtype=np.float64))
        field = f.create_group('Data').create_dataset('DENS', data=index.reshape(shape))
        field.attrs.update(metadata_version='candidate-field-1', unit='unknown',
                           centering='cell', basis='scalar', meaning='synthetic_linear_index',
                           unit_reason='Synthetic storage workload, not physical density')
    assert path.stat().st_size < 64 * 1024 * 1024
    return {'case': f'synthetic-{blocks}-blocks', 'localEvidenceDirectory': str(directory),
            'shape': list(shape), 'cells': cells, 'fileBytes': path.stat().st_size,
            'scientificReference': False, 'storage': 'contiguous FP64, no compression',
            'sourceIdentity': 'not recorded; synthetic fixture only'}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root', required=True, type=Path)
    args = parser.parse_args()
    root = args.output_root.resolve()
    # Existence is an error: retain all previous evidence, never overwrite.
    root.mkdir(parents=True, exist_ok=False)
    runs = [write_fixture(root / f'blocks-{n}', n) for n in (4, 256)]
    (root / 'runs.json').write_text(json.dumps(runs, indent=2) + '\n')
    print(json.dumps({'version': 'synthetic-query-workload-1', 'runs': runs}, indent=2))


if __name__ == '__main__':
    main()
