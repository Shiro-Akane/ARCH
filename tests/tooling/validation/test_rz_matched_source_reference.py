"""Reference identities and rejection behavior, not physical acceptance."""
import sys
import json
import subprocess
import tempfile
import unittest
from decimal import Decimal, localcontext
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/"validation/gravity"))
import rz_matched_source_reference as rz
from rz_ring_axis_reference import axis_reference

class MatchedSourceTests(unittest.TestCase):
    def source(self):
        return {"sourceId":"explicit-fixture-not-runtime","leaves":[
            dict(id="a",r_lower=0,r_upper=1,z_lower=-1,z_upper=0,density=2),
            dict(id="b",r_lower=0,r_upper=1,z_lower=0,z_upper=1,density=2)]}

    def test_axis_partition_and_decimal_preservation(self):
        with localcontext() as ctx:
            ctx.prec=80
            a=rz.reference(self.source(),dict(r_observer=0,z_observer=3))
            b=axis_reference(0,1,-1,1,2,3,80,decimal_output=True)
            for key in a:self.assertLess(abs(a[key]-b[key]),Decimal("1e-70"))
            self.assertEqual(axis_reference(0,1,-1,1,2,3),
                             {k:float(v) for k,v in b.items()})

    def test_exterior_partition(self):
        whole={"sourceId":"whole","leaves":[dict(id="w",r_lower=0,r_upper=1,z_lower=-1,z_upper=1,density=2)]}
        observer=dict(r_observer=4,z_observer=3)
        a=rz.reference(self.source(),observer,order=16)
        b=rz.reference(whole,observer,order=16)
        for key in a:
            self.assertLess(abs((a[key]-b[key])/b[key]),Decimal("1e-18"))

    def test_heterogeneous_source_is_sum_not_average_density(self):
        s=self.source();s["leaves"][1]["density"]=7
        observer=dict(r_observer=0,z_observer=3)
        with localcontext() as ctx:
            ctx.prec=80
            expected=[axis_reference(0,1,-1,0,2,3,80,decimal_output=True),
                      axis_reference(0,1,0,1,7,3,80,decimal_output=True)]
            actual=rz.reference(s,observer)
            self.assertEqual(actual,{k:expected[0][k]+expected[1][k] for k in actual})

    def test_contact_contributor_cannot_be_silently_skipped(self):
        for z in (-.5,0,1):
            with self.assertRaisesRegex(ValueError,"Interior/contact"):
                rz.reference(self.source(),dict(r_observer=.5,z_observer=z),order=8)

    def test_source_identity_overlap_and_invalid_density(self):
        for mutation in ("identity","duplicate","overlap","negative","nan"):
            s=self.source()
            if mutation=="identity":s["sourceId"]=""
            if mutation=="duplicate":s["leaves"][1]["id"]="a"
            if mutation=="overlap":s["leaves"][1]["z_lower"]=-.5
            if mutation=="negative":s["leaves"][0]["density"]=-1
            if mutation=="nan":s["leaves"][0]["density"]=float("nan")
            with self.assertRaises(ValueError):
                rz.reference(s,dict(r_observer=0,z_observer=3))

    def test_zero_source_and_invalid_settings(self):
        s=self.source()
        for leaf in s["leaves"]:leaf["density"]=0
        actual=rz.reference(s,dict(r_observer=.5,z_observer=0),order=8)
        self.assertTrue(all(v==0 for v in actual.values()))
        for observer,kwargs in [(dict(r_observer=-1,z_observer=0),{}),
                                (dict(r_observer=0,z_observer=0),{"order":1}),
                                (dict(r_observer=0,z_observer=0),{"precision":20})]:
            with self.assertRaises(ValueError):rz.reference(s,observer,**kwargs)
    def test_separate_contact_potential_keeps_force_rejected(self):
        s=self.source();observer=dict(r_observer=.5,z_observer=0)
        result=rz.potential_reference(s,observer,order=8)
        self.assertEqual(result["contactLeaves"],2)
        self.assertFalse(result["certified"])
        self.assertLess(result["potential"],0)
        self.assertNotIn("radial_acceleration",result)
        with self.assertRaisesRegex(ValueError,"Interior/contact"):
            rz.reference(s,observer,order=8)
        with self.assertRaises(ValueError):
            rz.potential_reference(s,observer,order=8,t_panels=0)

    def test_cli_retains_failure_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            base=Path(folder);inp=base/"source.json";out=base/"summary.json"
            source=self.source()
            source["observers"]=[dict(id="axis",r_observer=0,z_observer=0),
                                 dict(id="contact",r_observer=.5,z_observer=0)]
            inp.write_text(json.dumps(source))
            command=[sys.executable,str(Path(rz.__file__)),"--input",str(inp),
                     "--output",str(out),"--order","8"]
            run=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            report=json.loads(out.read_text())
            self.assertEqual([r["status"] for r in report["rows"]],
                             ["ESTIMATE","UNSUPPORTED_OR_FAILED"])
            self.assertNotIn("leaves",report)
            previous=out.read_bytes()
            run=subprocess.run(command,capture_output=True,text=True)
            self.assertNotEqual(run.returncode,0)
            self.assertEqual(out.read_bytes(),previous)

if __name__=="__main__":unittest.main()
