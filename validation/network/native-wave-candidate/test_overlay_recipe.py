"""Local mechanical-rewrite contracts only. Does not compile C++ or run CUDA."""
import hashlib
import importlib.util
import json
import posixpath
from pathlib import Path
import re
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
spec = importlib.util.spec_from_file_location('wave_overlay', HERE / 'make_overlay.py')
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)


class OverlayRecipe(unittest.TestCase):
    def test_pinned_scheduler(self):
        raw = (ROOT / overlay.SCHEDULER).read_bytes()
        result = overlay.rewrite_scheduler(raw).decode()
        self.assertIn('provider_->execute(wave_tasks_).require_success();', result)
        self.assertNotIn('provider_->factorize(', result)
        self.assertNotIn('provider_->solve(', result)
        self.assertNotIn('cached_factor_token_', result)
        self.assertEqual(result.count('std::make_unique<experimental::CuDssSparseWaveSolver>'), 1)

    def test_changed_kernel_rejected(self):
        raw = (ROOT / overlay.SCHEDULER).read_bytes()
        with self.assertRaisesRegex(ValueError, 'canonical scheduler changed'):
            overlay.rewrite_scheduler(raw.replace(b'lane >= count', b'lane > count', 1))

    def test_changed_controller_rejected(self):
        raw = (ROOT / overlay.SCHEDULER).read_bytes()
        with self.assertRaisesRegex(ValueError, 'canonical scheduler changed'):
            overlay.rewrite_scheduler(raw + b'\n// different base\n')

    def test_ambiguous_anchor_rejected(self):
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            overlay.replace_once('twice twice', 'twice', 'one')

    def test_missing_anchor_rejected(self):
        with self.assertRaisesRegex(ValueError, 'missing'):
            overlay.replace_once('original', 'absent', 'new')

    def test_new_overlay_has_no_runtime_claim(self):
        with tempfile.TemporaryDirectory() as temp:
            dest = Path(temp) / 'overlay'
            before = (ROOT / overlay.SCHEDULER).read_bytes()
            record = overlay.prepare(ROOT, HERE, dest)
            self.assertEqual(record['status'], 'prepared_not_compiled_not_runtime_validated')
            self.assertFalse(record['release_qualified'])
            self.assertIsNone(record['pending'])
            self.assertEqual(len(record['files']), 13)
            self.assertEqual((ROOT / overlay.SCHEDULER).read_bytes(), before)
            for path, item in record['files'].items():
                data = (dest / path).read_bytes()
                self.assertEqual(item, dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
            self.assertEqual(json.loads((dest / 'overlay-record.json').read_text()), record)
            with self.assertRaisesRegex(ValueError, 'output must be new'):
                overlay.prepare(ROOT, HERE, dest)

    def test_provider_shared_include_closure(self):
        # The standalone provider contract may not silently take a mathematical
        # dependency from an older factory tree. This is an inventory check,
        # not a preprocessor/compiler substitute or an ODE overlay qualification.
        shared = overlay.REQUIRED_SHARED
        for relative in shared:
            text = (ROOT / relative).read_text()
            for include in re.findall(r'^\s*#include\s+"([^"]+)"', text, re.MULTILINE):
                local = posixpath.normpath(posixpath.join(posixpath.dirname(relative), include))
                rooted = posixpath.normpath('src/' + include)
                self.assertTrue(local in shared or rooted in shared,
                    f'project dependency absent from pinned provider inputs: {relative} -> {include}')

    def test_canonical_output_rejected(self):
        with self.assertRaisesRegex(ValueError, 'canonical project'):
            overlay.prepare(ROOT, HERE, ROOT / 'src' / 'must-not-create-native-wave')
        self.assertFalse((ROOT / 'src' / 'must-not-create-native-wave').exists())

    def test_changed_canonical_input_rejected_before_output(self):
        with tempfile.TemporaryDirectory() as temp:
            candidate = Path(temp) / 'candidate'
            candidate.mkdir()
            manifest = json.loads((HERE / 'shared-inputs.json').read_text())
            first = next(iter(manifest['sha256']))
            manifest['sha256'][first] = '0' * 64
            (candidate / 'shared-inputs.json').write_text(json.dumps(manifest))
            out = Path(temp) / 'out'
            with self.assertRaisesRegex(ValueError, 'SHA mismatch'):
                overlay.prepare(ROOT, candidate, out)
            self.assertFalse(out.exists())

    def test_unsafe_manifest_paths_rejected_before_output(self):
        for bad in ('', '../escape', '/escape', 'src/../../escape', 'src\\bad', 'tests/unreviewed'):
            with self.subTest(path=bad), tempfile.TemporaryDirectory() as temp:
                candidate = Path(temp) / 'candidate'
                candidate.mkdir()
                (candidate / 'shared-inputs.json').write_text(json.dumps(dict(sha256={bad: '0'*64})))
                out = Path(temp) / 'out'
                with self.assertRaisesRegex(ValueError, 'unsafe'):
                    overlay.prepare(ROOT, candidate, out)
                self.assertFalse(out.exists())

    def test_missing_inventory_rejected_before_output(self):
        with tempfile.TemporaryDirectory() as temp:
            candidate = Path(temp) / 'candidate'
            candidate.mkdir()
            manifest = json.loads((HERE / 'shared-inputs.json').read_text())
            manifest['sha256'].pop(next(iter(manifest['sha256'])))
            (candidate / 'shared-inputs.json').write_text(json.dumps(manifest))
            out = Path(temp) / 'out'
            with self.assertRaisesRegex(ValueError, 'inventory differs'):
                overlay.prepare(ROOT, candidate, out)
            self.assertFalse(out.exists())

    def test_extra_inventory_rejected_before_output(self):
        with tempfile.TemporaryDirectory() as temp:
            candidate = Path(temp) / 'candidate'
            candidate.mkdir()
            manifest = json.loads((HERE / 'shared-inputs.json').read_text())
            manifest['sha256']['src/unreviewed.h'] = '0' * 64
            (candidate / 'shared-inputs.json').write_text(json.dumps(manifest))
            out = Path(temp) / 'out'
            with self.assertRaisesRegex(ValueError, 'inventory differs'):
                overlay.prepare(ROOT, candidate, out)
            self.assertFalse(out.exists())


if __name__ == '__main__':
    unittest.main()
