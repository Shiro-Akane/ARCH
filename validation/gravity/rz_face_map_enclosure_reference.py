#!/usr/bin/env python3
"""Exact native face/B mapping and actual canonical RHS construction evidence.
Raw arrays remain local; all published metrics are scalar. No production math
helpers are imported and no scientific threshold is fitted.
"""
import argparse,hashlib,json,subprocess
from pathlib import Path
from fractions import Fraction as F
from decimal import Decimal as D,localcontext
from rz_ring_axis_reference import PI
from rz_stencil_construction_reference import reconstruct

def main():
    p=argparse.ArgumentParser();p.add_argument('--probe',required=True,type=Path)
    p.add_argument('--output-root',required=True,type=Path);a=p.parse_args()
    out=a.output_root.resolve()
    if out.exists():p.error('output-root must be new')
    out.mkdir(parents=True)
    q=subprocess.run([str(a.probe.resolve()),'rz-stencil-probe'],capture_output=True,text=True)
    (out/'probe.json').write_text(q.stdout);(out/'stderr.log').write_text(q.stderr)
    if q.returncode:raise SystemExit(q.returncode)
    rows=[];faces=boundary_faces=cells=0
    for c in json.loads(q.stdout)['cases']:
        neighbors=[set() for _ in c['cells']]
        for face in c['faces']:
            if face['left']>=0 and face['right']>=0:
                neighbors[face['left']].add(face['right']);neighbors[face['right']].add(face['left'])
        def width(i,axis):return F(c['spacing'][axis])/2**c['cells'][i]['level']
        def radial(i):
            lo=F(c['origin'][0])+c['cells'][i]['index'][0]*width(i,0)
            return (lo+width(i,0))**2-lo**2
        volumes=[radial(i)*width(i,1) for i in range(len(c['cells']))]
        total=sum(volumes,F(0));weights=[v/total for v in volumes]
        ideal_rhs=[F(0) for _ in volumes];stored_rhs=[F(0) for _ in volumes]
        max_center=max_quotient=max_map=F(0);max_area=0.;nf=0
        for index,f in enumerate(c['faces']):
            _,bc,center=reconstruct(c,f,neighbors);boundary=f['boundary_side']>=0
            anchor=f['left'] if f['left']>=0 else f['right']
            fine=anchor if boundary else (f['left'] if c['cells'][f['left']]['level']>=c['cells'][f['right']]['level'] else f['right'])
            area_no_pi=2*center[0]*width(fine,1) if f['axis']==0 else radial(fine)
            for axis,x in enumerate(center):
                assert F(f['center_lower'][axis])<=x<=F(f['center_upper'][axis])
                err=abs(F(f['center'][axis])-x);assert err<=F(f['center_error_upper'][axis])
                max_center=max(max_center,err)
            for precision in (100,140):
                with localcontext() as ctx:
                    ctx.prec=precision
                    area=PI*D(area_no_pi.numerator)/D(area_no_pi.denominator)
                    assert D.from_float(f['area_lower'])<=area<=D.from_float(f['area_upper'])
                    error=abs(D.from_float(f['area'])-area)
                    assert error<=D.from_float(f['area_error_upper']);max_area=max(max_area,float(error))
            if boundary:nf+=1
            for side,i in enumerate((f['left'],f['right'])):
                if i<0:
                    assert all(f[k][side]==0 for k in ('area_over_volume_lower','area_over_volume_upper',
                        'area_over_volume_error_upper','boundary_map_lower','boundary_map_upper','boundary_map_error_upper'))
                    continue
                ratio=area_no_pi/volumes[i]
                stored_ratio=F(f['area'])/F(c['stored_volumes'][i])
                assert F(f['area_over_volume_lower'][side])<=ratio<=F(f['area_over_volume_upper'][side])
                error=abs(ratio-stored_ratio);assert error<=F(f['area_over_volume_error_upper'][side])
                max_quotient=max(max_quotient,error)
                if not boundary:
                    assert all(f[k][side]==0 for k in ('boundary_map_lower','boundary_map_upper','boundary_map_error_upper'))
                    continue
                sign=1 if side==0 else -1
                ideal_map=sign*ratio*bc;stored_map=sign*stored_ratio*F(f['boundary_coefficient'])
                assert F(f['boundary_map_lower'][side])<=ideal_map<=F(f['boundary_map_upper'][side])
                error=abs(ideal_map-stored_map);assert error<=F(f['boundary_map_error_upper'][side])
                max_map=max(max_map,error)
                value=F(c['face_values'][index]);ideal_rhs[i]+=ideal_map*value;stored_rhs[i]+=stored_map*value
        construction_errors=[abs(ideal-stored) for ideal,stored in zip(ideal_rhs,stored_rhs)]
        combined_errors=[abs(ideal-F(actual)) for ideal,actual in zip(ideal_rhs,c['rhs'])]
        for i in range(len(volumes)):
            assert construction_errors[i]<=F(c['construction_cells'][i])
            assert combined_errors[i]<=F(c['combined_cells'][i])
        for errors,upper in ((construction_errors,c['construction_native_norm_upper']),
                             (combined_errors,c['combined_native_norm_upper'])):
            assert sum((w*e**2 for w,e in zip(weights,errors)),F(0))<=F(upper)**2
        assert sum((w*F(e)**2 for w,e in zip(weights,c['combined_cells'])),F(0))<=F(c['combined_native_norm_upper'])**2
        faces+=len(c['faces']);boundary_faces+=nf;cells+=len(volumes)
        rows.append({'mixed':bool(c['mixed']),'hierarchyLevel':c.get('hierarchy_level',-1),
            'radialOrigin':c['origin'][0],'spacing':c['spacing'][:2],'cells':len(volumes),'faces':len(c['faces']),
            'boundaryFaces':nf,'maximumExactCenterDiscrepancy':float(max_center),
            'maximumAreaDiscrepancy':max_area,'maximumExactAreaOverVolumeDiscrepancy':float(max_quotient),
            'maximumExactBoundaryMapDiscrepancy':float(max_map),
            'constructionNativeNormUpper':c['construction_native_norm_upper'],
            'constructionPlusAssemblyNativeNormUpper':c['combined_native_norm_upper']})
    result={'status':'PASS','cases':len(rows),'cells':cells,'faces':faces,'boundaryFaces':boundary_faces,'rows':rows,
        'probeSha256':hashlib.sha256(a.probe.read_bytes()).hexdigest(),
        'scope':'Ideal native face geometry and selected-stencil B construction, plus actual canonical RHS assembly for supplied manufactured face values',
        'limitations':['Face values do not certify potential at ideal source/observer coordinates',
            'Interior A construction, full physical residual, Phi/force and recovery coverage remain separate',
            'No tolerance change, simulation, CUDA or long-run claim; raw arrays retained locally']}
    (out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print('RZ_FACE_MAP_ENCLOSURE_PASS',len(rows),cells,faces,boundary_faces)
if __name__=='__main__':main()
