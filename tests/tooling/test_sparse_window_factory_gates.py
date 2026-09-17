"""Synthetic pre-build evidence checks, not CUDA or scientific qualification."""
import ast
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT/'validation/network/native-wave-candidate/windowed'
saved = sys.path[:]
sys.path.insert(0, str(HERE))
try:
    spec = importlib.util.spec_from_file_location('window_factory_gate_tests', HERE/'build_factory.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
finally:
    sys.path[:] = saved


class WindowFactoryGateTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.collection = dict(contracts_pass=True, worker_exit_code=0, contract_count=18,
                               nuclear_qualified=False, performance_qualified=False, release_qualified=False)
        for kind in ('raw', 'compact'):
            path = self.root/(kind+'.synthetic')
            data = ('synthetic '+kind+' bytes, not actual evidence').encode()
            path.write_bytes(data)
            self.collection[kind] = dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        self.receipt = dict(status='both_archives_and_all_members_byte_verified',
                            raw=copy.deepcopy(self.collection['raw']), compact=copy.deepcopy(self.collection['compact']))
        self.save()

    def save(self):
        (self.root/'window-collection-v1.json').write_text(json.dumps(self.collection))
        (self.root/'window-local-receipt-v1.json').write_text(json.dumps(self.receipt))

    def test_two_matching_archives_and_scoped_contract_result_required(self):
        paths = module.require_backup(self.root)
        self.assertEqual([p.name for p in paths], ['window-collection-v1.json', 'window-local-receipt-v1.json'])

    def test_missing_failed_incomplete_or_overstated_contract_refuses_build(self):
        for key, value in (('contracts_pass', False), ('worker_exit_code', 1), ('contract_count', 17),
                           ('nuclear_qualified', True), ('performance_qualified', True), ('release_qualified', True)):
            original = self.collection[key]
            self.collection[key] = value
            self.save()
            with self.subTest(key=key), self.assertRaises(ValueError): module.require_backup(self.root)
            self.collection[key] = original

    def test_both_archive_identities_and_receipt_status_are_checked(self):
        for kind in ('raw', 'compact'):
            path = Path(self.collection[kind]['path'])
            old = path.read_bytes()
            path.write_bytes(old+b'changed')
            with self.subTest(kind=kind), self.assertRaises(ValueError): module.require_backup(self.root)
            path.write_bytes(old)
        self.receipt['status'] = 'downloaded_not_verified'
        self.save()
        with self.assertRaises(ValueError): module.require_backup(self.root)

    def test_recipe_is_build_only_and_never_reuses_existing_factory_objects(self):
        text = (HERE/'build_factory.py').read_text()
        ast.parse(text)
        self.assertIn("('factory', factory, 12000)", text)
        self.assertIn('verify_factory_dependencies(dependency.read_text()', text)
        self.assertIn("'factory-built-not-runtime-qualified'", text)
        self.assertNotIn('trajectory_command', text)
        self.assertNotIn('dispatch.sh', text)
        self.assertNotIn('--use_fast_math', text)


if __name__ == '__main__':
    unittest.main()
