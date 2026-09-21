"""Real CPU session reuse, transport bounds and isolated-request equivalence."""
import hashlib
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest

ARCH, ROOT = map(lambda p: Path(p).resolve(), sys.argv[1:3])
del sys.argv[1:3]
ENV = dict(os.environ, OMP_NUM_THREADS='1', CUDA_VISIBLE_DEVICES='')
BASE = 'nblockx1=1\nnblockx2=0\nnblockx3=0\nnetwork_name=none\nx1_min=0\nx1_max=1\n'
TABLE = ROOT/'EOS_toolkit/tables/helmholtz/helm_table.dat'
CELL = (ROOT/'simulation/Cellular/CellularPreview2D.par').read_text()+f'\neos_table_path={TABLE}\nuse_burn=false\n'
HOT = (ROOT/'simulation/CooperativeHotspots/CooperativeHotspots.par').read_text()+f'\neos_table_path={TABLE}\n'


class Session:
    def __init__(self, cwd):
        self.process = subprocess.Popen([str(ARCH), '--preview-session'], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, cwd=cwd, env=ENV)
        self.events = queue.Queue()
        def read():
            for line in self.process.stdout:
                try:
                    self.events.put(json.loads(line))
                except Exception as error:
                    self.events.put(error)
            self.events.put(None)
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()
        self.ready = self.next()
        assert self.ready['kind'] == 'preview-session-ready', self.ready

    def next(self):
        result = self.events.get(timeout=360)
        if isinstance(result, Exception):
            raise result
        return result

    def send(self, obj):
        data = json.dumps(obj).encode()+b'\n' if isinstance(obj, dict) else obj
        self.process.stdin.write(data)
        self.process.stdin.flush()

    def result(self):
        progress = []
        while True:
            obj = self.next()
            assert obj is not None, 'unexpected session EOF'
            if obj['kind'] != 'preview-session-progress':
                return obj, progress
            progress.append(obj)

    def call(self, case='Sod', config=BASE, command='--preview', **options):
        self.send(dict(command=command, caseId=case, configText=config, requestId='edit-初期🌌', **options))
        return self.result()

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        self.process.wait(timeout=10)
        self.reader.join(timeout=10)
        self.process.stdin.close()
        self.process.stdout.close()


class PreviewSession(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='arch-session-')
        self.addCleanup(self.temp.cleanup)
        self.cwd = Path(self.temp.name)
        self.session = Session(self.cwd)
        self.addCleanup(self.session.close)

    def single(self, case, text, command, *args):
        proc = subprocess.run([str(ARCH), command, case, '--config-stdin', '--request-id', 'edit-初期🌌', *args],
            input=text, text=True, capture_output=True, cwd=self.cwd, env=ENV, timeout=360)
        self.assertEqual(proc.returncode, 0, proc.stdout[-4000:])
        return json.loads(proc.stdout)

    def check(self, result, text, progress):
        self.assertEqual(result['kind'], 'preview-session-result')
        self.assertEqual(result['exitCode'], 0, result)
        self.assertEqual(result['identity'], result['response']['identity'])
        self.assertEqual(result['identity']['configRevision'], hashlib.sha256(text.encode()).hexdigest())
        self.assertFalse(result['resources']['resultReused'])
        self.assertTrue(progress)
        self.assertTrue(all(p['identity']==result['identity'] and p['sequence']==result['sequence'] for p in progress))
        self.assertGreaterEqual(result['elapsedMilliseconds'], 0)

    def test_discovery_and_sod_edits_match_fresh_process(self):
        caps = json.loads(subprocess.check_output([str(ARCH), '--preview-capabilities'], env=ENV))
        self.assertEqual(self.session.ready['capability'], caps['extensions']['session'])
        before = list(self.cwd.iterdir())
        values = []
        for x in [.25, .75, .25]:
            text = BASE+f'x_pos={x}\n'
            got, progress = self.session.call(config=text, samples=8)
            self.check(got, text, progress)
            expected = self.single('Sod', text, '--preview', '--samples', '8')
            self.assertEqual(got['response']['execution'],expected['execution'])
            evaluation=got['resources']['sampleEvaluation']
            self.assertEqual((evaluation['initCalls'],evaluation['eosConversions'],evaluation['exactStateReuses']),(8,2,6))
            for part in ('data', 'state', 'parameterMetadata', 'graphicalBindings'):
                self.assertEqual(got['response'][part], expected[part])
            values.append(got['response']['data'])
        self.assertEqual(values[0], values[2])
        self.assertNotEqual(values[0], values[1])
        self.assertEqual(list(self.cwd.iterdir()), before, 'preview created files')

    def test_helm_reuse_keeps_full_cellular_fields_and_new_values(self):
        first, progress = self.session.call(case='CellularDet', config=CELL, samplesX1=5, samplesX2=3)
        self.check(first, CELL, progress)
        self.assertEqual(first['resources']['tableLoads'], 1)
        for extra in ('radiusPerturb=12\n', 'tempPerturb=2.e9\n'):
            text = CELL+extra
            warm, progress = self.session.call(case='CellularDet', config=text, samplesX1=5, samplesX2=3)
            self.check(warm, text, progress)
            self.assertEqual(warm['resources']['tableLoads'], 0)
            self.assertGreater(warm['resources']['tableHits'], 0)
            expected = self.single('CellularDet', text, '--preview', '--samples-x1', '5', '--samples-x2', '3')
            self.assertEqual(warm['response']['data'], expected['data'])
            self.assertEqual(warm['response']['state'], expected['state'])
            self.assertNotEqual(first['response']['data'], warm['response']['data'])

    def test_iterative_hotspot_reuse_and_fresh_setup(self):
        first, progress = self.session.call(case='CooperativeHotspots', config=HOT, command='--inspect-case')
        self.check(first, HOT, progress)
        self.assertEqual(first['resources']['tableLoads'], 1)
        text = HOT+'hotspot_center_x=48\nhotspot_temperature=2.8e9\n'
        warm, progress = self.session.call(case='CooperativeHotspots', config=text, command='--inspect-case')
        self.check(warm, text, progress)
        self.assertEqual(warm['resources']['tableLoads'], 0)
        expected = self.single('CooperativeHotspots', text, '--inspect-case')
        for part in ('data', 'state', 'parameterMetadata'):
            self.assertEqual(warm['response'][part], expected[part])
        self.assertNotEqual(first['response']['data'], warm['response']['data'])

    def test_mesh_field_and_error_recovery(self):
        text=BASE+'nblockx1=4\nx_pos=.43\nlrefinemax=3\nmax_blocks=128\nrefine_threshold=.1\nderefine_threshold=.01\n'
        mesh, progress=self.session.call(config=text, command='--preview-amr', meshMaxBlocks=128)
        self.check(mesh,text,progress)
        expected=self.single('Sod',text,'--preview-amr','--mesh-max-blocks','128')
        self.assertEqual(mesh['response']['data'],expected['data'])
        failed,_=self.session.call(config=BASE+'x_pos=5\n',samples=8)
        self.assertNotEqual(failed['exitCode'],0)
        self.assertTrue(failed['resources']['clearedAfterError'])
        self.assertIsNone(failed['response']['data'])
        ok,progress=self.session.call(config=BASE,samples=8)
        self.check(ok,BASE,progress)

    def test_table_change_reset_and_recovery(self):
        copied=self.cwd/'table.dat'
        shutil.copyfile(TABLE,copied)
        text=CELL+f'eos_table_path={copied}\n'
        first,_=self.session.call(case='CellularDet',config=text,samplesX1=2,samplesX2=2)
        self.assertEqual(first['resources']['tableLoads'],1)
        # Same length and mtime, but different valid content: never rely on stat.
        stat=copied.stat()
        with copied.open('r+b') as f:
            prefix=f.read(256); index=prefix.index(b' '); f.seek(index); f.write(b'\t')
        os.utime(copied,ns=(stat.st_atime_ns,stat.st_mtime_ns))
        changed,_=self.session.call(case='CellularDet',config=text,samplesX1=2,samplesX2=2)
        self.assertEqual(changed['resources']['tableLoads'],1)
        self.assertNotEqual(first['response']['state']['eos']['sourceFingerprint'],changed['response']['state']['eos']['sourceFingerprint'])
        self.session.send(dict(command='reset-resources',requestId='reset'))
        self.assertEqual(self.session.result()[0]['kind'],'preview-session-reset')
        copied.unlink()
        failed,_=self.session.call(case='CellularDet',config=text,samplesX1=2,samplesX2=2)
        self.assertNotEqual(failed['exitCode'],0)
        self.assertEqual(failed['resources']['retainedTables'],0)
        ok,progress=self.session.call(samples=8)
        self.check(ok,BASE,progress)

    def test_strict_frames_and_recovery(self):
        valid=dict(command='--preview',caseId='Sod',configText=BASE,requestId='valid',samples=8)
        invalid = [b'{"command":"--preview","command":"--preview"}\n', b'{"x":01}\n',
            b'{"x":1.0}\n', b'{"x":true}\n', b'{"x":[]}\n', b'{"x":"\\ud800"}\n',
            b'{"x":"\\u0000"}\n', b'{"x":"\xff"}\n', b'{}x\n', b'{"x":1,}\n']
        invalid += [dict(valid, samples=2.5), dict(valid, samples=2**40), dict(valid, future=1),
                    dict(valid, configText='x'*(1024*1024+1)),dict(valid,command='--list-cases')]
        for bad in invalid:
            self.session.send(bad)
            error,_=self.session.result()
            self.assertEqual(error['kind'],'preview-session-error',error)
            self.assertFalse(error['fatal'])
        self.session.send(valid)
        good,progress=self.session.result()
        self.check(good,BASE,progress)

    def test_eof_frame_limit_and_recycle(self):
        self.session.process.stdin.write(b'{')
        self.session.process.stdin.close()
        error,_=self.session.result()
        self.assertTrue(error['fatal'])
        self.assertEqual(self.session.process.wait(timeout=10),2)
        with subprocess.Popen([str(ARCH),'--preview-session'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE,cwd=self.cwd,env=ENV) as p:
            stdout,_=p.communicate(b' '* (8*1024*1024+1)+b'\n',timeout=30)
            self.assertEqual(p.returncode,2)
            self.assertTrue(json.loads(stdout.splitlines()[-1])['fatal'])
        other=Session(self.cwd)
        try:
            for n in range(other.ready['capability']['maxRequests']):
                other.send(dict(command='reset-resources',requestId=str(n)))
                self.assertEqual(other.result()[0]['kind'],'preview-session-reset')
            self.assertEqual(other.next()['reason'],'request-limit')
            self.assertEqual(other.process.wait(timeout=10),0)
        finally:
            other.close()

    def test_cancel_setup_and_restart(self):
        self.session.send(dict(command='--inspect-case',caseId='CooperativeHotspots',configText=HOT,requestId='cancel'))
        while True:
            event=self.session.next()
            self.assertIsNotNone(event)
            if event.get('stage')=='setup':
                break
        self.session.close()
        self.assertNotEqual(self.session.process.returncode,0)
        replacement=Session(self.cwd)
        try:
            ok,progress=replacement.call(samples=8)
            self.check(ok,BASE,progress)
        finally:
            replacement.close()

if __name__=='__main__':
    unittest.main()
