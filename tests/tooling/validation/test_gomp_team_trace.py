"""Real Linux/GNU region observations; never an ARCH science/performance test."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]

class GompTraceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temp.name)
        cls.library = cls.directory/"trace.so"
        subprocess.run(["/usr/bin/c++","-std=c++20","-O2","-fPIC","-shared",
            "-fopenmp","-Wall","-Wextra","-pedantic",str(ROOT/"tools/gomp_team_trace.cpp"),
            "-ldl","-o",str(cls.library)],check=True)
        source=cls.directory/"fixture.cpp"
        source.write_text("""
#include <atomic>
#include <cstdio>
#include <omp.h>
int main(int argc,char**) {
 std::atomic<int> calls{0};
 const int regions=argc==1?1:130;
 for(int r=0;r<regions;++r) {
  #pragma omp parallel
  calls.fetch_add(1);
 }
 #pragma omp parallel if(false)
 calls.fetch_add(1);
 std::printf("%d\\n",calls.load());
}
""")
        cls.fixture=cls.directory/"fixture"
        subprocess.run(["/usr/bin/c++","-O2","-fopenmp",str(source),"-o",str(cls.fixture)],check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_fixture(self, threads, many=False, tracing=True):
        env=os.environ.copy()
        env.pop("ARCH_GOMP_TRACE_FD",None)
        env.update(LD_PRELOAD=str(self.library),OMP_NUM_THREADS=str(threads),
                   OMP_DYNAMIC="FALSE",OMP_PLACES="threads",OMP_PROC_BIND="spread")
        args=[str(self.fixture)]+(["many"] if many else [])
        if not tracing:
            p=subprocess.run(args,env=env,capture_output=True,check=True,timeout=10)
            return int(p.stdout),[]
        with tempfile.TemporaryFile() as trace:
            env["ARCH_GOMP_TRACE_FD"]=str(trace.fileno())
            p=subprocess.run(args,env=env,pass_fds=(trace.fileno(),),
                             capture_output=True,check=True,timeout=10)
            trace.seek(0)
            rows=[json.loads(line) for line in trace.read().splitlines()]
        return int(p.stdout),rows

    def test_actual_team_and_serialized_region_do_not_change_body(self):
        calls,rows=self.run_fixture(8)
        self.assertEqual(calls,9)
        self.assertEqual([r["team"] for r in rows[:-1]],[8,1])
        self.assertEqual(rows[-1]["teamSizeCounts"],{"1":1,"8":1})
        for r in rows[:-1]:
            self.assertEqual({m["thread"] for m in r["members"]},set(range(r["team"])))
            for m in r["members"]:
                self.assertTrue(m["readable"])
                self.assertIn(m["cpu"],m["affinity"])

    def test_histogram_counts_all_regions_after_record_limit(self):
        calls,rows=self.run_fixture(2,many=True)
        self.assertEqual(calls,261)
        self.assertEqual(len(rows)-1,128)
        self.assertEqual(rows[-1]["interceptedRegions"],131)
        self.assertEqual(rows[-1]["teamSizeCounts"],{"1":1,"2":130})
        self.assertEqual(rows[-1]["oversizedTeams"],0)

    def test_unset_trace_descriptor_forwards_without_records(self):
        calls,rows=self.run_fixture(8,tracing=False)
        self.assertEqual(calls,9)
        self.assertEqual(rows,[])

if __name__=="__main__":
    unittest.main()
