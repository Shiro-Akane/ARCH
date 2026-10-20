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


def write_fixture(directory: Path, blocks: int, storage: str = 'contiguous') -> dict:
    if storage not in ('contiguous', 'chunked', 'gzip'):
        raise ValueError('Unsupported storage layout')
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
        def dataset(group, key, value):
            options = {}
            if storage != 'contiguous':
                options['chunks'] = (1, ny, nx) if value.ndim == 3 else (min(value.size, 4096),)
            if storage == 'gzip':
                options.update(compression='gzip', compression_opts=4)
            return group.create_dataset(key, data=value, **options)

        grid = f.create_group('Grid')
        grid.attrs.update(coordinate_unit='cm', coordinate_basis='cartesian')
        for key, value in [('x', lower_x + .5), ('y', lower_y + .5), ('z', zero)]:
            dataset(grid, key, value)
        dataset(grid, 'level', np.zeros(blocks, dtype=np.int32))
        dataset(grid, 'morton', np.arange(blocks, dtype=np.uint64))
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
            dataset(native, f'x{axis}_lower', low)
            dataset(native, f'x{axis}_upper', high)
            dataset(native, f'logical_x{axis}', np.arange(blocks, dtype=np.uint32) if axis == 1 else np.zeros(blocks, dtype=np.uint32))
        dataset(native, 'cell_measure', np.ones(cells, dtype=np.float64))
        field = dataset(f.create_group('Data'), 'DENS', index.reshape(shape))
        field.attrs.update(metadata_version='candidate-field-1', unit='unknown',
                           centering='cell', basis='scalar', meaning='synthetic_linear_index',
                           unit_reason='Synthetic storage workload, not physical density')
    assert path.stat().st_size < 64 * 1024 * 1024
    return {'case': f'synthetic-{blocks}-blocks', 'localEvidenceDirectory': str(directory),
            'shape': list(shape), 'cells': cells, 'fileBytes': path.stat().st_size,
            'scientificReference': False, 'storage': storage, 'compression': 'gzip level 4' if storage == 'gzip' else 'none',
            'sourceIdentity': 'not recorded; synthetic fixture only'}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root', required=True, type=Path)
    parser.add_argument('--storage', choices=('contiguous', 'chunked', 'gzip'), default='contiguous',
                        help='Synthetic layout only; does not select production writer policy')
    args = parser.parse_args()
    root = args.output_root.resolve()
    # Existence is an error: retain all previous evidence, never overwrite.
    root.mkdir(parents=True, exist_ok=False)
    runs = [write_fixture(root / f'blocks-{n}', n, args.storage) for n in (4, 256)]
    (root / 'runs.json').write_text(json.dumps(runs, indent=2) + '\n')
    print(json.dumps({'version': 'synthetic-query-workload-1', 'runs': runs}, indent=2))


if __name__ == '__main__':
    main()
