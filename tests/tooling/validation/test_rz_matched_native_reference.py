"""Actual-record adapter rejection and geometry identity, not science gates."""
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/"validation/gravity"))
import rz_matched_native_reference as rz

class NativeAdapterTests(unittest.TestCase):
    def case(self):
        return dict(source_identity=dict(topology=9,time=0,G=6.6743e-8,
            operator_revision=1,boundary_revision=1,accuracy_revision=1,generation=1,
            inputs=[dict(uid=1,epoch=9,slot=0,version=1,storage_generation=1)]),
            origin=[0,-.5],spacing=[1,1],
            cells=[dict(level=0,index=[0,0],edges=[0,1,-.5,.5],density=3)])
    def test_explicit_density_not_rhs(self):
        c=self.case();source=rz.source_from_case(c)
        self.assertEqual(source["leaves"][0]["density"],3)
        c["source"]=[999] # changing RHS cannot replace explicit density
        self.assertEqual(source,rz.source_from_case(c))
        del c["cells"][0]["density"]
        with self.assertRaises(KeyError):rz.source_from_case(c)
    def test_identity_and_density_change_hash(self):
        c=self.case();before=rz.source_from_case(c)["sourceId"]
        for field in ("generation","time"):
            changed=copy.deepcopy(c);changed["source_identity"][field]+=1
            self.assertNotEqual(before,rz.source_from_case(changed)["sourceId"])
        c["cells"][0]["density"]=4
        self.assertNotEqual(before,rz.source_from_case(c)["sourceId"])
    def test_malformed_stamp_rejected(self):
        for mutate in ("epoch","revision","inputs","G"):
            c=self.case()
            if mutate=="epoch":c["source_identity"]["inputs"][0]["epoch"]=10
            if mutate=="revision":c["source_identity"]["operator_revision"]=0
            if mutate=="inputs":c["source_identity"]["inputs"]=[]
            if mutate=="G":c["source_identity"]["G"]=float("nan")
            with self.assertRaises(ValueError):rz.source_from_case(c)
    def test_actual_edges_must_match_root(self):
        c=self.case();c["cells"][0]["edges"][1]=1.0000000000000002
        with self.assertRaisesRegex(ValueError,"Rounded"):rz.source_from_case(c)
if __name__=="__main__":unittest.main()
