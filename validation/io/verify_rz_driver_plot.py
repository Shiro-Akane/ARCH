"""Independent native-cell readback of the actual internal RZ Driver fixture."""
import argparse, hashlib, json
from decimal import Decimal, localcontext
from pathlib import Path
import h5py
import numpy as np

PI=Decimal("3.141592653589793238462643383279502884197169399375105820974944592307816406286")
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--evidence-root",required=True)
    ap.add_argument("--summary",required=True)
    args=ap.parse_args()
    root=Path(args.evidence_root);rows=[]
    for rz,mode in [(False,"cartesian"),(True,"rz")]:
        path=next((root/mode).glob("*_plt_*.h5"));before=sha(path)
        with h5py.File(path) as f:
            assert float(f.attrs["time"])==0.
            assert f.attrs["plot_publication_state"]=="complete"
            assert f.attrs["plot_publication_method"]=="checked-close-atomic-replace"
            n=f["NativeGrid"];shape=f["Data/DENS"].shape
            if not rz:
                assert "geometry_chart" not in f.attrs
                assert n.attrs["version"]=="candidate-cartesian-1"
                assert n.attrs["measure_unit"]=="cm"
                assert "VELZ" not in f["Data"]
                rows.append(dict(mode=mode,shape=list(shape),fileSha256=before,legacyMetadataPreserved=True))
                continue
            assert shape==(1,16,16)
            assert int(f.attrs["geometry_semantics_revision"])==2
            assert f.attrs["geometry_chart"]=="axisymmetric-rz"
            assert n.attrs["version"]=="candidate-axisymmetric-rz-2"
            assert n.attrs["measure_convention"]=="full-rotation-axisymmetric-ring"
            assert n.attrs["measure_unit"]=="cm^3" and n.attrs["measure_normalization"]=="full_rotation"
            assert n.attrs["x1_axis"]=="r_cy" and n.attrs["x2_axis"]=="z_cy"
            assert n.attrs["native_coordinate_unit"]=="cm"
            lo=n["x1_lower"][()];hi=n["x1_upper"][()]
            zlo=n["x2_lower"][()];zhi=n["x2_upper"][()]
            rad=.5*(lo+hi);z=.5*(zlo+zhi)
            assert zlo.min()==-4. and zhi.max()==4.
            np.testing.assert_array_equal(f["Grid/x"][()],rad)
            np.testing.assert_array_equal(f["Grid/y"][()],np.zeros_like(rad))
            np.testing.assert_array_equal(f["Grid/z"][()],z)
            checks={}
            for field,expected,meaning in [
                ("VELX",.1*rad,"radial_velocity"),
                ("VELY",.2*z,"axial_velocity"),
                ("VELZ",.3*rad,"representative_azimuthal_velocity")]:
                ds=f["Data/"+field]
                assert ds.dtype==np.dtype("float64")
                np.testing.assert_array_equal(ds[()].reshape(-1),expected)
                assert ds.attrs["basis"]=="local-orthonormal-r-z-phi"
                assert ds.attrs["meaning"]==meaning and ds.attrs["unit"]=="cm/s"
                checks[field]=0.
            # Real Driver materialization imposes physical ghost rules before
            # writing: radial axis odd r/phi, outer-r and z outflow copies.
            # Use an independent Decimal face-flux / centered-curl stencil at
            # boundary cells, and the continuum result in the interior.
            with localcontext() as ctx:
                ctx.prec=80
                reference=[];div_reference=[];vort_reference=[]
                for index,(a,b,c,d) in enumerate(zip(lo,hi,zlo,zhi)):
                    a,b,c,d=map(lambda v:Decimal.from_float(float(v)),(a,b,c,d))
                    rr=(a+b)/2;zz=(c+d)/2;dr=b-a;dz=d-c
                    j,i=divmod(index,16)
                    rm=rr-dr;rp=rr if i==15 else rr+dr
                    # At r=0, odd reflection equals the linear extension.
                    zm=zz if j==0 else zz-dz
                    zp=zz if j==15 else zz+dz
                    volume=PI*(b*b-a*a)*dz
                    radial=2*PI*dz*(b*Decimal(".1")*(rr+rp)/2
                        -a*Decimal(".1")*(rr+rm)/2)
                    axial=PI*(b*b-a*a)*Decimal(".2")*(zp-zm)/2
                    reference.append(float(volume))
                    div_reference.append(float((radial+axial)/volume))
                    vort_reference.append(float(Decimal(".3")+Decimal(".3")*(rp-rm)/(2*dr)))
                reference=np.array(reference)
            for field,expected,analytic in [("DIVV",div_reference,.4),("VORT",vort_reference,.6)]:
                stored=f["Data/"+field][()].reshape(-1)
                error=float(np.max(np.abs(stored-np.array(expected))))
                assert error<2e-12
                interior=stored.reshape(16,16)[1:-1,:-1]
                analytic_error=float(np.max(np.abs(interior-analytic)))
                assert analytic_error<2e-12
                checks[field]=dict(allCellsBoundaryDiscreteMaxError=error,
                    interiorAnalyticMaxError=analytic_error)
            assert f.attrs["state_semantics"]=="rz-m-phi-j-over-w-v1"
            angular=f["NativeState"]
            assert angular.attrs["version"]=="candidate-rz-angular-1"
            w=n["angular_measure"][()]
            assert w.dtype==np.dtype("float64")
            assert n["angular_measure"].attrs["unit"]=="cm^4"
            m=angular["m_phi"][()].reshape(-1)
            ell=angular["angular_momentum_density"][()].reshape(-1)
            assert angular["m_phi"].shape==shape and angular["m_phi"].dtype==np.dtype("float64")
            assert angular["angular_momentum_density"].dtype==np.dtype("float64")
            assert angular["m_phi"].attrs["unit"]=="g/(cm^2*s)"
            assert angular["angular_momentum_density"].attrs["unit"]=="g/(cm*s)"
            # Fixture prescribes raw m_phi=2*(.3*r_mid), not a new rotating Init reference.
            np.testing.assert_array_equal(m,2*(.3*rad))
            np.testing.assert_array_equal(f["Data/VELZ"][()].reshape(-1),m/2)
            assert f["Data/VELZ"].attrs["averaging"]=="representative-m_phi-over-rho"
            assert f["Data/DENS"].attrs["averaging"]=="native-volume-average"
            assert f["Data/ENER"].attrs["averaging"]=="native-volume-average"
            w_reference=[]
            ell_reference=[]
            with localcontext() as ctx:
                ctx.prec=80
                for a,b,c,d,mm in zip(lo,hi,zlo,zhi,m):
                    a,b,c,d,mm=map(lambda v:Decimal.from_float(float(v)),(a,b,c,d,mm))
                    ww=2*PI*(b**3-a**3)*(d-c)/3
                    vv=PI*(b*b-a*a)*(d-c)
                    w_reference.append(float(ww));ell_reference.append(float(mm*ww/vv))
            w_error=float(np.max(np.abs(w-np.array(w_reference))/np.array(w_reference)))
            ell_error=float(np.max(np.abs(ell-np.array(ell_reference))))
            assert w_error<2e-12 and ell_error<2e-12
            actual=n["cell_measure"][()]
            np.testing.assert_array_equal(ell,(m*w)/actual)
            metric_relative=float(np.max(np.abs(actual-reference)/reference))
            assert metric_relative<2e-12
            volume=float(np.sum(actual));assert abs(volume-float(8*PI))<2e-12
            assert np.all(f["Data/DENS"][()]==2.) and np.all(f["Data/ENER"][()]==100.)
            rows.append(dict(mode=mode,shape=list(shape),cells=len(rad),fileSha256=before,
                chart="axisymmetric-rz",coordinateMapping="(r,z) -> Cartesian (r,0,z)",
                measureUnit="cm^3",normalization="full_rotation",
                fullDomainVolume=volume,metricMaxRelativeError=metric_relative,
                angularMeasureMaxRelativeError=w_error,angularDensityMaxAbsoluteError=ell_error,
                rawMPhiPreserved=True,representativeVelocityDistinguished=True,
                fieldMaxAbsoluteErrors=checks,publication="checked-close-atomic-replace"))
        assert sha(path)==before
    out=Path(args.summary);assert not out.exists()
    result=dict(status="PASS",scope="Actual Driver IO-only fixture; analytical velocity and independent Decimal ring measure, no timestep advancement",
        arithmeticGate=2e-12,gateOrigin="Existing internal CPU geometry engineering gate; not owner scientific acceptance",
        rows=rows,limitations=["No public RZ capability","No RZ Viewer support","No gravity or angular-momentum transfer acceptance","No CUDA"])
    out.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps(result,indent=2))
if __name__=="__main__":main()
