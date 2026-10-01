"""Offline importer for this specific 1D Cartesian Cellular run, not a generic AMR reader."""
from pathlib import Path
import hashlib, json
import h5py
import numpy as np
root = Path(__file__).resolve().parents[2]
run = root / 'output/cellular_run_20260913_011131'
frames = []
for index in (0, 1):
    source = run / f'CDet_1D_HLLC_plt_{index:04d}.h5'
    with h5py.File(source) as f:
        assert int(f.attrs['dim']) == 1 and f.attrs['geometry'] == 'cartesian'
        shape = f['Data/DENS'].shape
        assert shape == (46, 16)
        x = f['Grid/x'][:].reshape(shape)
        dx = np.diff(x, axis=1)
        assert np.all(dx > 0) and np.allclose(dx, dx[:, :1], rtol=0, atol=1e-12)
        left = x[:, 0] - dx[:, 0] / 2
        right = x[:, -1] + dx[:, 0] / 2
        blocks = np.argsort(left)
        assert np.allclose(right[blocks][:-1], left[blocks][1:], rtol=0, atol=1e-12)
        assert abs(left.min()) < 1e-12 and abs(right.max()-128) < 1e-12
        order = np.argsort(x.ravel())
        xs = x.ravel()[order]
        assert np.all(np.diff(xs)>0)
        fields = {}
        for name in ('DENS','TEMP','PRES'):
            values = f['Data/'+name][:].ravel()[order]
            assert np.all(np.isfinite(values))
            fields[name] = values.tolist()
        frames.append(dict(label='Initial' if index == 0 else 'Final (step 200)', time=float(f.attrs['time']), source=str(source.relative_to(root)), sha256=hashlib.sha256(source.read_bytes()).hexdigest(), x=xs.tolist(), levels=np.repeat(f['Grid/level'][:],16)[order].tolist(), fields=fields))
out = root/'studio/src/samples/cellular.json'
out.write_text(json.dumps(dict(case='CellularDet',dim=1,geometry='cartesian',domain=[0,128],cells=736,blocks=46,steps=200,tmax=5e-8,termination='max_steps',frames=frames),allow_nan=False,separators=(',',':'))+'\n')
print(f'Imported {len(frames)} snapshots / 736 cells each to {out}; no overlap, gaps or non-finite fields.')
