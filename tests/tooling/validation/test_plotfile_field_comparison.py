"""Cross-build exact-field checker counterexamples, all H5 files remain temporary."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
import h5py
import numpy as np
scripts=Path(__file__).resolve().parents[3]/"validation/io"
sys.path.insert(0,str(scripts))
spec=importlib.util.spec_from_file_location("field_comparison",scripts/"compare_plotfile_fields.py")
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class FieldComparisonTests(unittest.TestCase):
    def fixtures(self, root):
        rows=[]
        for name in ["current","prior"]:
            d=root/name; (d/"output").mkdir(parents=True)
            config=d/"Sod.par";config.write_text(f"out_dir={d}/output\ncfl=.4\n")
            row={"case":"Sod","localEvidenceDirectory":str(d),
                 "inputSha256":module.sha(config),"binarySha256":("a" if name=="current" else "b")*64}
            with h5py.File(d/"output/fixture_plt_0000.h5","w") as h:
                h.attrs["time"]=0
                for key,value in [("case_id","Sod"),("raw_config_sha256",row["inputSha256"]),
                                  ("binary_sha256",row["binarySha256"])]:
                    h.require_group("SourceIdentity").attrs[key]=value
                for key in ["Grid/level","Grid/morton",*["NativeGrid/logical_x"+str(i) for i in (1,2,3)]]:
                    h.create_dataset(key,data=np.array([0],dtype=np.int32))
                h.create_dataset("Data/DENS",data=np.array([[1.,-0.]],dtype=np.float64))
            rows.append(row)
        return rows

    def test_same_raw_bits_with_different_binary_and_output_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            rows=self.fixtures(Path(temp))
            result=module.compare(*rows)
            self.assertTrue(result["allFieldBitsMatch"])
            self.assertEqual(result["changedInputKeys"],["out_dir"])
            self.assertNotEqual(result["currentBinarySha256"],result["priorBinarySha256"])

    def test_negative_zero_and_one_ulp_changes_are_detected(self):
        for column,value in [(1,0.),(0,np.nextafter(1.,2.))]:
            with tempfile.TemporaryDirectory() as temp:
                rows=self.fixtures(Path(temp))
                with h5py.File(Path(rows[0]["localEvidenceDirectory"])/"output/fixture_plt_0000.h5","r+") as h:
                    h["Data/DENS"][0,column]=value
                result=module.compare(*rows)
                self.assertFalse(result["allFieldBitsMatch"])
                self.assertEqual(result["fields"]["DENS"]["bitMismatchCount"],1)

    def test_changed_physics_input_and_block_order_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            rows=self.fixtures(Path(temp))
            config=Path(rows[0]["localEvidenceDirectory"])/"Sod.par"
            config.write_text(config.read_text().replace(".4",".5"))
            rows[0]["inputSha256"]=module.sha(config)
            with self.assertRaisesRegex(ValueError,"Input differs"):
                module.compare(*rows)
        with tempfile.TemporaryDirectory() as temp:
            rows=self.fixtures(Path(temp))
            with h5py.File(Path(rows[0]["localEvidenceDirectory"])/"output/fixture_plt_0000.h5","r+") as h:
                h["Grid/morton"][0]=1
            with self.assertRaisesRegex(ValueError,"ordering"):
                module.compare(*rows)

if __name__ == "__main__":
    unittest.main()
