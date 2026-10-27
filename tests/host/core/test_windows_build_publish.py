"""Native-only publication controls; test helper is never installed.

Run: python test_windows_build_publish.py path/to/test-helper.exe
This checks OS semantics, not science files or power-loss durability.
"""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
import uuid

PROGRAM = str(Path(sys.argv.pop(1)).resolve())
KERNEL = ctypes.WinDLL('kernel32', use_last_error=True)
KERNEL.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
    wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
KERNEL.CreateFileW.restype = wintypes.HANDLE
KERNEL.CloseHandle.argtypes = [wintypes.HANDLE]
TARGET = 'build-' + uuid.uuid4().hex[:24] + '.json'


class PublishTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix='arch publish 中文 '))
        self.directory = self.root / 'studio' / '.local'
        self.directory.mkdir(parents=True)
        self.target = self.directory / TARGET
        self.old = json.dumps({'old': True, 'padding': 'a' * 16384}).encode()
        self.new = json.dumps({'new': True, 'padding': 'b' * 16384}).encode()
        self.target.write_bytes(self.old)

    def tearDown(self):
        shutil.rmtree(str(self.root))

    def command(self, fail=None, pause=None, target=TARGET, cwd=None):
        env = dict(os.environ)
        if fail:
            env['ARCH_PUBLISH_TEST_FAIL'] = fail
        if pause:
            env['ARCH_PUBLISH_TEST_PAUSE'] = pause
        return subprocess.Popen([PROGRAM, str(self.directory), target,
            '.build-' + str(uuid.uuid4()) + '.tmp'], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, cwd=str(cwd or self.root))

    def complete(self, child, data=None, expect=0):
        stdout, stderr = child.communicate(self.new if data is None else data, timeout=15)
        self.assertEqual(child.returncode, expect, stderr.decode(errors='replace'))
        return json.loads(stdout), stderr

    def no_temps(self):
        self.assertEqual(list(self.directory.glob('.build-*.tmp')), [])

    def test_new_and_replace_unicode(self):
        self.target.unlink()
        for _ in range(2):
            result, _ = self.complete(self.command())
            self.assertEqual(result, {'committed': True})
            self.assertEqual(self.target.read_bytes(), self.new)
            self.no_temps()

    def test_rename_is_relative_to_source_parent_not_cwd(self):
        outside = self.root / 'outside-cwd'
        outside.mkdir()
        for cwd in (self.root, outside):
            self.complete(self.command(cwd=cwd))
            self.assertEqual(self.target.read_bytes(), self.new)
            self.assertFalse((cwd / TARGET).exists())
            self.no_temps()

    def test_published_file_has_owner_private_protected_dacl(self):
        self.complete(self.command())
        advapi = ctypes.WinDLL('advapi32', use_last_error=True)
        acl, descriptor = ctypes.c_void_p(), ctypes.c_void_p()
        advapi.GetNamedSecurityInfoW.argtypes = [wintypes.LPWSTR, wintypes.DWORD,
            wintypes.DWORD, ctypes.c_void_p, ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p,
            ctypes.POINTER(ctypes.c_void_p)]
        self.assertEqual(advapi.GetNamedSecurityInfoW(str(self.target), 1, 4,
            None, None, ctypes.byref(acl), None, ctypes.byref(descriptor)), 0)
        try:
            control, revision = wintypes.WORD(), wintypes.DWORD()
            advapi.GetSecurityDescriptorControl.argtypes = [ctypes.c_void_p,
                ctypes.POINTER(wintypes.WORD), ctypes.POINTER(wintypes.DWORD)]
            self.assertTrue(advapi.GetSecurityDescriptorControl(descriptor,
                ctypes.byref(control), ctypes.byref(revision)))
            self.assertTrue(control.value & 0x1000)  # SE_DACL_PROTECTED
            count = ctypes.c_ushort.from_address(acl.value + 4).value
            self.assertEqual(count, 2)
            advapi.GetAce.argtypes = [ctypes.c_void_p, wintypes.DWORD,
                ctypes.POINTER(ctypes.c_void_p)]
            advapi.ConvertSidToStringSidW.argtypes = [ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_void_p)]
            sids = []
            KERNEL.LocalFree.argtypes = [ctypes.c_void_p]
            for index in range(count):
                ace, sid_text = ctypes.c_void_p(), ctypes.c_void_p()
                self.assertTrue(advapi.GetAce(acl, index, ctypes.byref(ace)))
                self.assertEqual(ctypes.c_ubyte.from_address(ace.value).value, 0)
                self.assertTrue(advapi.ConvertSidToStringSidW(ace.value + 8,
                    ctypes.byref(sid_text)))
                try:
                    sids.append(ctypes.wstring_at(sid_text))
                finally:
                    KERNEL.LocalFree(sid_text)
            self.assertIn('S-1-5-18', sids)  # SYSTEM
            # One explicit owner ACE alongside SYSTEM; inherited broad groups
            # must not enter this newly published file's DACL.
            self.assertEqual(len(set(sids)), 2)
            self.assertNotIn('S-1-1-0', sids)
            self.assertNotIn('S-1-5-11', sids)
        finally:
            KERNEL.LocalFree(descriptor)

    def test_precommit_failures_preserve_old_and_cleanup(self):
        for stage in ('created', 'write', 'flush', 'close', 'close-api', 'replace'):
            result, _ = self.complete(self.command(fail=stage), expect=1)
            self.assertFalse(result['committed'], stage)
            self.assertTrue(result['cleanup'], stage)
            self.assertEqual(self.target.read_bytes(), self.old, stage)
            self.no_temps()

    def test_postcommit_failure_reports_replaced(self):
        for stage in ('committed', 'close-api-committed'):
            result, _ = self.complete(self.command(fail=stage), expect=1)
            self.assertTrue(result['committed'])
            self.assertEqual(self.target.read_bytes(), self.new)
            self.no_temps()

    def test_oversized_and_empty_input(self):
        for data in (b'', b'x' * (4 * 1024 * 1024 + 1)):
            result, _ = self.complete(self.command(), data, expect=1)
            self.assertFalse(result['committed'])
            self.assertEqual(self.target.read_bytes(), self.old)
            self.no_temps()

    def open_handle(self, target, access, share, flags=0):
        handle = KERNEL.CreateFileW(str(target), access, share, None, 3, flags, None)
        self.assertNotEqual(handle, ctypes.c_void_p(-1).value, ctypes.get_last_error())
        self.addCleanup(KERNEL.CloseHandle, handle)
        return handle

    def test_existing_target_without_share_delete(self):
        handle = self.open_handle(self.target, 0x80000000, 1)
        result, _ = self.complete(self.command(), expect=1)
        self.assertFalse(result['committed'])
        self.assertEqual(self.target.read_bytes(), self.old)
        self.no_temps()
        self.assertTrue(KERNEL.CloseHandle(handle))
        self._cleanups.pop()

    def test_held_target_blocks_then_release_allows_replace(self):
        handle = self.open_handle(self.target, 0x80000000, 1 | 2 | 4)
        # Ordinary Windows replacement can reject even a share-delete reader.
        # Keep that failure explicit; do not force POSIX rename semantics.
        result, _ = self.complete(self.command(), expect=1)
        self.assertFalse(result['committed'])
        self.assertEqual(self.target.read_bytes(), self.old)
        self.no_temps()
        self.assertTrue(KERNEL.CloseHandle(handle))
        self._cleanups.pop()
        self.complete(self.command())
        self.assertEqual(self.target.read_bytes(), self.new)

    def test_pinned_ancestors_reject_rename(self):
        child = self.command(pause='pinned')
        try:
            self.assertEqual(child.stderr.readline(), b'checkpoint\r\n')
            for directory in (self.directory, self.directory.parent, self.root):
                with self.assertRaises(OSError):
                    os.rename(str(directory), str(directory) + '-rebound')
                write_handle = KERNEL.CreateFileW(str(directory), 0x40000000, 1 | 2 | 4, None, 3, 0x02000000 | 0x00200000, None)
                if write_handle != ctypes.c_void_p(-1).value:
                    KERNEL.CloseHandle(write_handle)
                    self.fail('Pinned directory unexpectedly accepted a writable handle')
                self.assertEqual(ctypes.get_last_error(), 32)
            child.stdin.write(b'!')
            child.stdin.flush()
            self.complete(child)
        finally:
            if child.poll() is None:
                child.kill()
                child.communicate()
        self.no_temps()

    def test_preexisting_delete_handle_blocks_pin(self):
        handle = self.open_handle(self.directory, 0x00010000, 1 | 2 | 4,
            0x02000000 | 0x00200000)
        result, _ = self.complete(self.command(), expect=1)
        self.assertFalse(result['committed'])
        self.assertEqual(self.target.read_bytes(), self.old)
        self.assertTrue(KERNEL.CloseHandle(handle))
        self._cleanups.pop()
        self.no_temps()

    def test_junction_directory_rejected(self):
        alternate = self.root / 'other'
        alternate.mkdir()
        junction = self.root / 'junction'
        subprocess.run(['cmd.exe', '/d', '/c', 'mklink', '/J', str(junction),
            str(alternate)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        real = self.directory
        try:
            self.directory = junction
            result, _ = self.complete(self.command(), expect=1)
            self.assertFalse(result['committed'])
            self.assertEqual(list(alternate.iterdir()), [])
        finally:
            self.directory = real
            os.rmdir(str(junction))
        self.assertEqual(self.target.read_bytes(), self.old)

    def test_bad_native_names_rejected(self):
        for target in ('../outside', 'C:/outside', 'x:stream', TARGET + ' ', 'CON'):
            result, _ = self.complete(self.command(target=target), expect=1)
            self.assertFalse(result['committed'])
            self.assertEqual(self.target.read_bytes(), self.old)
        self.no_temps()

    def test_readers_never_observe_partial_content(self):
        stop = threading.Event()
        observations = []
        failures = []
        denied = []

        def reader():
            while not stop.is_set():
                try:
                    data = self.target.read_bytes()
                    if data not in (self.old, self.new):
                        failures.append(len(data))
                    observations.append(len(data))
                except PermissionError:
                    # Windows may deny an open while the newly renamed source
                    # handle is still exclusive. This never establishes freshness.
                    denied.append(True)

        thread = threading.Thread(target=reader)
        thread.start()
        try:
            for _ in range(4):
                child = self.command()
                stdout, stderr = child.communicate(self.new, timeout=15)
                # A reader holding a target without share-delete may also deny
                # replacement; a failure must preserve the previous complete file.
                self.assertIn(child.returncode, (0, 1), stderr)
                result = json.loads(stdout)
                if child.returncode == 1:
                    self.assertFalse(result['committed'])
                self.no_temps()
        finally:
            stop.set()
            thread.join(10)
        self.assertTrue(observations)
        self.assertEqual(failures, [])
        self.assertFalse(thread.is_alive())


if __name__ == '__main__':
    unittest.main(verbosity=2)
