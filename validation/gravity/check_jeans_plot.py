#!/usr/bin/env python3
"""Read-only independent bounded IdealGas JENS Plotfile qualification.

Uses native physical cell bounds and actual input FP64 gamma/state. No Core
EOS/Jeans routine supplies the reference. This is not a trajectory oracle.
Raw HDF5 never enters the emitted processed scalar summary.
"""
import argparse
from decimal import Decimal, localcontext
import json
from pathlib import Path
import h5py
import numpy as np

def qualify(path, previous=None):
    with h5py.File(path, "r") as h:
        if h.attrs.get("plot_publication_state") != "complete":
            raise ValueError("incomplete scientific publication")
        if "JENS" not in h["Data"]:
            raise ValueError("missing real JENS array")
        data=h["Data/JENS"]
        if data.dtype != np.dtype("float64"):
            raise ValueError("JENS is not native FP64")
        for key,value in {"unit":"1", "basis":"scalar", "centering":"cell",
            "meaning":"jeans_length_over_max_active_physical_spacing"}.items():
            if data.attrs.get(key) != value:
                raise ValueError(f"incorrect JENS {key}")
        source=h["SourceIdentity"]
        if source.attrs.get("eos_type") != "ideal" or source.attrs.get("species_count") != 1:
            raise ValueError("bounded reference requires one caloric IdealGas species")
        gamma=float(source["species_gamma"][0])
        if not np.isfinite(gamma) or gamma<=1:
            raise ValueError("invalid authoritative species gamma")
        dim=int(h.attrs["dim"])
        spacing=[]
        grid_evidence="explicit-native-bounds"
        if "NativeGrid" in h:
            chart=h["NativeGrid"].attrs.get("center_basis")
            if chart not in ("cartesian","local-orthonormal-r-z-phi","axisymmetric-rz","cylindrical-r-z"):
                if h["NativeGrid"].attrs.get("version")!="candidate-rz-1":
                    raise ValueError(f"unsupported reference chart: {chart}")
            native=h["NativeGrid"]
            for axis in range(1,dim+1):
                lo=np.asarray(native[f"x{axis}_lower"]).reshape(-1)
                hi=np.asarray(native[f"x{axis}_upper"]).reshape(-1)
                spacing.append(hi-lo)
        elif dim==3 and h.attrs.get("geometry")=="cartesian":
            # Existing 3D writer stores actual native Cartesian cell centers.
            # Accept only an exactly uniform, fully matched tensor lattice;
            # never infer spacing from config defaults or irregular samples.
            grid_evidence="legacy-3d-exact-native-center-lattice"
            shape=data.shape
            if len(shape)!=4:raise ValueError("unexpected legacy 3D block shape")
            count=int(np.prod(shape[1:]))
            centers=[np.asarray(h["Grid"][axis]).reshape(shape[0],count)
                     for axis in ("x","y","z")]
            widths=[[] for _ in range(3)]
            for block in range(shape[0]):
                axes=[np.unique(c[block]) for c in centers]
                if [len(x) for x in axes]!=list(reversed(shape[1:])):
                    raise ValueError("native Cartesian center counts mismatch")
                for axis,values in enumerate(axes):
                    delta=np.diff(values)
                    if len(delta)==0 or delta[0]<=0 or not np.all(delta==delta[0]):
                        raise ValueError("legacy center lattice is not exactly uniform")
                    widths[axis].extend([delta[0]]*count)
                z,y,x=np.meshgrid(axes[2],axes[1],axes[0],indexing="ij")
                if not all(np.array_equal(c[block],v.reshape(-1))
                           for c,v in zip(centers,[x,y,z])):
                    raise ValueError("legacy native tensor mapping mismatch")
            spacing=[np.asarray(x) for x in widths]
        else:
            raise ValueError("missing supported authoritative native grid evidence")
        rho=np.asarray(h["Data/DENS"]).reshape(-1)
        energy=np.asarray(h["Data/ENER"]).reshape(-1)
        velocity=[np.asarray(h["Data"][key]).reshape(-1) if key in h["Data"]
                  else np.zeros_like(rho) for key in ("VELX","VELY","VELZ")]
        actual=np.asarray(data).reshape(-1)
        if any(len(x)!=len(actual) for x in [rho,energy,*velocity,*spacing]):
            raise ValueError("native mapping shape mismatch")
        maximum=0.
        with localcontext() as ctx:
            ctx.prec=120
            pi=Decimal("3.14159265358979323846264338327950288419716939937510582097494459230781640628620899862803482534211706798214808651328230665")
            gravity=Decimal("6.67430e-8")
            g=Decimal.from_float(gamma)
            for n,value in enumerate(actual):
                density=Decimal.from_float(float(rho[n]))
                internal=Decimal.from_float(float(energy[n]))/density-sum(
                    Decimal.from_float(float(v[n]))**2 for v in velocity)/2
                cs2=g*(g-1)*internal
                length=(pi*cs2/(gravity*density)).sqrt()
                width=max(Decimal.from_float(float(s[n])) for s in spacing)
                expected=length/width
                if not np.isfinite(value) or value<=0:
                    raise ValueError("nonpositive/nonfinite JENS output")
                maximum=max(maximum,float(abs(Decimal.from_float(float(value))-expected)/expected))
        bound=16*np.finfo(np.float64).eps
        if maximum>bound:
            raise ValueError(f"independent JENS reference exceeded frozen static bound: {maximum} > {bound}")
        unchanged=[]
        if previous:
            with h5py.File(previous,"r") as old:
                for name in old["Data"]:
                    if name in ("DENS","ENER","VELX","VELY","VELZ","fixture-gas"):
                        if not np.array_equal(np.asarray(old["Data"][name]),np.asarray(h["Data"][name])):
                            raise ValueError(f"output-only altered {name}")
                        unchanged.append(name)
        return {"file":str(path),"cells":len(actual),"shape":list(data.shape),
                "dtype":str(data.dtype),"gridEvidence":grid_evidence,"maxRelativeError":maximum,"bound":float(bound),
                "unchangedFields":unchanged,"publication":"complete",
                "sourceIdentityScope":str(source.attrs.get("scope","unknown")),
                "scope":"bounded static single-caloric-species native writer; not evolved JENS acceptance"}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("plot",type=Path)
    p.add_argument("--previous",type=Path)
    a=p.parse_args()
    print(json.dumps(qualify(a.plot,a.previous),indent=2))
if __name__=="__main__":
    main()
