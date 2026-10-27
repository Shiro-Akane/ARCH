"""Native config and read-handle controls; test helper is never installed."""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import uuid

PROGRAM = str(Path(sys.argv.pop(1)).resolve())
ADVAPI = ctypes.WinDLL('advapi32', use_last_error=True)
KERNEL = ctypes.WinDLL('kernel32', use_last_error=True)
KERNEL.LocalFree.argtypes = [ctypes.c_void_p]
ADVAPI.GetNamedSecurityInfoW.argtypes = [wintypes.LPWSTR, wintypes.DWORD,
    wintypes.DWORD, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
    ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
ADVAPI.ConvertSecurityDescriptorToStringSecurityDescriptorW.argtypes = [
    ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
    ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p]


def dacl(path):
    descriptor, text = ctypes.c_void_p(), ctypes.c_void_p()
    assert ADVAPI.GetNamedSecurityInfoW(str(path), 1, 4, None, None, None,
        None, ctypes.byref(descriptor)) == 0
    try:
        assert ADVAPI.ConvertSecurityDescriptorToStringSecurityDescriptorW(
            descriptor, 1, 4, ctypes.byref(text), None)
        return ctypes.wstring_at(text)
    finally:
        KERNEL.LocalFree(text)
        KERNEL.LocalFree(descriptor)


class WindowsFileTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix='arch config 中文 '))
        self.target = self.root / '输入.par'
        self.target.write_bytes(b'old\r\n')

    def tearDown(self):
        shutil.rmtree(str(self.root))

    def publish(self, data=b'new\r\n', create=False, target=None, fail=None):
        env = dict(os.environ)
        if fail:
            env['ARCH_PUBLISH_TEST_FAIL'] = fail
        result = subprocess.run([PROGRAM, 'config-create' if create else 'config-save',
            str(self.root), target or self.target.name,
            '.arch-config-' + str(uuid.uuid4()) + '.tmp', str(len(data))],
            input=data+b'!', stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            cwd=str(self.root.parent), env=env, timeout=15)
        lines = result.stdout.splitlines()
        receipt = json.loads(lines[-1])
        self.assertFalse(list(self.root.glob('.arch-config-*.tmp')))
        return result, receipt

    def test_original_inherited_dacl_entries_and_protection_preserved(self):
        before = dacl(self.target)
        result, receipt = self.publish()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(receipt['committed'])
        after = dacl(self.target)
        # The native fixture may start with inherited entries but no AI flag.
        # Windows may add that bookkeeping on save, as
        # in the protected-DACL case below. All permission entries and the
        # inheritance protection remain exact; AI is not an access grant.
        before_flags, before_entries = before.split('(', 1)
        after_flags, after_entries = after.split('(', 1)
        self.assertEqual(after_entries, before_entries)
        self.assertEqual(after_flags.replace('AI', ''), before_flags.replace('AI', ''))

    def test_create_private_protected_dacl_and_no_clobber(self):
        result, receipt = self.publish(create=True, target='new.par')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(dacl(self.root / 'new.par').startswith('D:P'))
        before = dacl(self.root / 'new.par')
        saved, _ = self.publish(target='new.par')
        self.assertEqual(saved.returncode, 0, saved.stderr)
        after = dacl(self.root / 'new.par')
        # SetSecurityInfo may set AI bookkeeping while keeping the exact entries
        # and protection. This contract preserves permissions, not the full SD.
        self.assertIn('P', after.split('(')[0])
        self.assertEqual(after[after.index('('):], before[before.index('('):])
        result, receipt = self.publish(create=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(receipt['committed'])
        self.assertEqual(self.target.read_bytes(), b'old\r\n')

    def test_null_dacl_is_refused_without_changing_original(self):
        ADVAPI.SetNamedSecurityInfoW.argtypes = [wintypes.LPWSTR, wintypes.DWORD,
            wintypes.DWORD, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
        self.assertEqual(ADVAPI.SetNamedSecurityInfoW(str(self.target), 1,
            4 | 0x80000000, None, None, None, None), 0)
        before = dacl(self.target)
        result, receipt = self.publish()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(receipt['committed'])
        self.assertEqual(self.target.read_bytes(), b'old\r\n')
        self.assertEqual(dacl(self.target), before)

    def test_precommit_write_flush_close_replace_failures_cleanup(self):
        for stage in ('created', 'write', 'flush', 'close', 'close-api', 'replace'):
            result, receipt = self.publish(fail=stage)
            self.assertNotEqual(result.returncode, 0, stage)
            self.assertFalse(receipt['committed'], stage)
            self.assertTrue(receipt['cleanup'], stage)
            self.assertEqual(self.target.read_bytes(), b'old\r\n', stage)

    def test_postcommit_error_does_not_claim_rollback(self):
        result, receipt = self.publish(fail='committed')
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(receipt['committed'])
        self.assertEqual(self.target.read_bytes(), b'new\r\n')

    def test_invalid_names_devices_streams_and_byte_budget_reject(self):
        for name in ('CON.par', 'NUL.par', '../outside.par', 'file.par:stream', 'file.PAR', 'bad.par.'):
            result, receipt = self.publish(target=name)
            self.assertNotEqual(result.returncode, 0, name)
            self.assertFalse(receipt['committed'])
        result, receipt = self.publish(data=b'x'*(1024*1024+1))
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(receipt['committed'])

    def test_unconfirmed_prepared_save_cleans_owned_temp(self):
        result = subprocess.run([PROGRAM, 'config-save', str(self.root), self.target.name,
            '.arch-config-'+str(uuid.uuid4())+'.tmp', '3'], input=b'new',
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(json.loads(result.stdout.splitlines()[-1])['committed'])
        self.assertEqual(self.target.read_bytes(), b'old\r\n')
        self.assertFalse(list(self.root.glob('.arch-config-*.tmp')))

    def test_force_killed_precommit_owner_deletes_only_its_temp(self):
        child = subprocess.Popen([PROGRAM, 'config-save', str(self.root), self.target.name,
            '.arch-config-'+str(uuid.uuid4())+'.tmp', '3'],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            self.assertEqual(json.loads(child.stdout.readline()), {'stage':'created'})
            child.stdin.write(b'new')
            child.stdin.flush()
            self.assertEqual(json.loads(child.stdout.readline()), {'stage':'prepared'})
            self.assertEqual(len(list(self.root.glob('.arch-config-*.tmp'))), 1)
            child.kill()
            child.communicate(timeout=15)
            deadline = time.monotonic()+2
            while list(self.root.glob('.arch-config-*.tmp')) and time.monotonic()<deadline:
                time.sleep(.02)
            self.assertFalse(list(self.root.glob('.arch-config-*.tmp')))
            self.assertEqual(self.target.read_bytes(), b'old\r\n')
        finally:
            if child.poll() is None:
                child.kill()
                child.communicate(timeout=15)

    def test_held_read_denies_write_delete_and_directory_rebinding(self):
        child = subprocess.Popen([PROGRAM, 'hold-read', str(self.root), self.target.name],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            self.assertEqual(json.loads(child.stdout.readline()), {'stage':'held'})
            for operation in (lambda:self.target.write_bytes(b'overwrite'),
                    lambda:self.target.unlink(), lambda:self.root.rename(str(self.root)+'-moved')):
                with self.assertRaises(OSError):
                    operation()
            stdout, stderr = child.communicate(b'!', timeout=15)
            self.assertEqual(child.returncode, 0, stderr)
            self.assertEqual(json.loads(stdout), {'released':True})
            self.target.write_bytes(b'released')
        finally:
            if child.poll() is None:
                child.kill()
                child.communicate(timeout=15)


if __name__ == '__main__':
    unittest.main(verbosity=2)
