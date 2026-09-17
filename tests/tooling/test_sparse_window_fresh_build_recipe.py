"""Preparation checks only: no local compiler, CUDA or server execution."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT/'validation/network/native-wave-candidate/windowed'
ARCHIVE = ROOT/'validation/network/results/native-wave-20260917/factory-focused-v1/records/ARCH-native-wave-v4-20260916'
RAW_SOURCE = ROOT/'build/native-wave-factory-focused-v1-verified/raw/ARCH-native-wave-v4-20260916/source'
saved = sys.path[:]
sys.path.insert(0, str(HERE))
try:
    spec = importlib.util.spec_from_file_location('window_fresh_factory_recipe', HERE/'factory_recipe.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
finally:
    sys.path[:] = saved


class FreshWindowBuildTests(unittest.TestCase):
    @unittest.skipUnless((RAW_SOURCE/'EOS_toolkit/tables/helmholtz/helm_table.dat').is_file(),
                         'complete source-copy test requires locally verified raw parent archive')
    def test_complete_copy_changes_only_two_execution_headers_plus_two_window_files(self):
        manifest = (ARCHIVE/'factory-control-v2/source-files.sha256').read_bytes()
        original, files = module.source_plan(RAW_SOURCE, manifest, HERE)
        self.assertEqual(len(files), 476)
        changed = {name for name in original if module.digest(files[name]) != original[name]}
        self.assertEqual(changed, set(module.PINS))
        # Quoted same-directory include must select the private executor.
        caller = 'src/cuda/microphysics/SparseBurnCells.cuh'
        self.assertIn(b'#include "SparseOdeBatch.cuh"', files[caller])
        target = str(Path(caller).parent/'SparseOdeBatch.cuh').replace('\\', '/')
        self.assertIn(b'CuDssSparseWindowSolver', files[target])
        self.assertEqual(module.digest(files[caller]), original[caller])
        with self.assertRaises(ValueError): module.source_plan(ARCHIVE/'source', manifest+b' ', HERE)

    def test_compact_projection_cannot_pretend_to_be_complete_source(self):
        manifest = (ARCHIVE/'factory-control-v2/source-files.sha256').read_bytes()
        with self.assertRaisesRegex(ValueError, 'helm_table.dat'):
            module.source_plan(ARCHIVE/'source', manifest, HERE)

    def test_existing_source_and_failed_preparation_are_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(ValueError):
                module.materialize_source('unused', b'', 'unused', Path(tmp))
            output = Path(tmp)/'new'
            with self.assertRaises(ValueError): module.materialize_source('unused', b'', 'unused', output)
            self.assertFalse(output.exists())

    def test_all_original_network_and_strict_flags_survive_fresh_compile(self):
        entries = json.loads((ARCHIVE/'factory-release/compile_commands.json').read_text())
        old = '/home/ubuntu/projects/ARCH-native-wave-v4-20260916/source'
        private = '/isolated/window/source'
        for network in (150, 200):
            for kind in ('factory', 'harness', 'wrapper'):
                with self.subTest(network=network, kind=kind):
                    result = module.compile_recipe(entries, network, kind, old, private, '/new/x.o', '/new/x.d')
                    argv = result['command']
                    self.assertEqual(argv[-5:], ['-MD', '-MF', '/new/x.d', '-MT', '/new/x.o'])
                    self.assertEqual(argv[argv.index('-o')+1], '/new/x.o')
                    self.assertIn('-I'+private+'/src', argv)
                    self.assertNotIn('-I'+old+'/src', argv)
                    module.strict(argv, kind == 'factory')
                    if kind != 'wrapper':
                        self.assertIn('-DARCH_TEST_NETWORK_TYPE=NetCustom_audit'+str(network), argv)
                    if kind == 'factory':
                        self.assertIn('--generate-code=arch=compute_90,code=[compute_90,sm_90]', argv)
                        self.assertIn('/home/ubuntu/projects/ARCH-large-networks-20260909/audit'+str(network)+'/NetCustom_audit'+str(network)+'.h', argv)

    def test_link_cannot_reuse_old_cuda_object(self):
        original = [':', '&&', '/usr/bin/g++-11', '-fno-fast-math', '-ffp-contract=off',
                    'old/test_generated_sparse_burn.cpp.o', 'old/test_generated_sparse_burn_factory.cu.o',
                    '-o', 'old-exe', 'libarch_cuda_sparse_provider.a', '/vendor/libcudss.so.0', '&&', ':']
        changed = module.link_recipe(original, '/new/host.o', '/new/factory.o', '/new/wrapper.o', '/frozen/provider.a', '/new/exe')
        self.assertEqual([name for name in changed if name.endswith('.o')],
                         ['/new/host.o', '/new/factory.o', '/new/wrapper.o'])
        self.assertIn('/vendor/libcudss.so.0', changed)
        for bad in (original[:4]+['-ffast-math']+original[4:], original[:5]+['old/extra.o']+original[5:]):
            with self.assertRaises(ValueError): module.link_recipe(bad, 'h', 'f', 'w', 'p', 'e')

    def test_actual_dependency_file_rejects_quoted_include_leakage(self):
        names = list(module.PINS)+['src/cuda/microphysics/SparseBurnCells.cuh',
                                  'src/cuda/microphysics/CuDssSparseWindowSolver.h']
        paths = ['/new/source/'+name for name in names]
        text = '/new/factory.o: '+' \\\n '.join(paths)+'\n'
        self.assertEqual(module.verify_factory_dependencies(text, '/new/factory.o', '/old/source', '/new/source'), paths)
        for bad in (text.replace('/new/factory.o:', '/old/factory.o:'),
                    text.replace(paths[0], '/old/source/'+names[0]),
                    text+' /old/source/hidden/header.h', text.replace(paths[-1], '')):
            with self.assertRaises(ValueError):
                module.verify_factory_dependencies(bad, '/new/factory.o', '/old/source', '/new/source')


if __name__ == '__main__':
    unittest.main()
