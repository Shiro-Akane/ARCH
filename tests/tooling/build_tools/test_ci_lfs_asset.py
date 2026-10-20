"""Required CI assets must reject pointers/corruption without fetching anything."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("ci_lfs_identity", ROOT / ".github/actions/lfs-asset/identity.py")
identity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(identity)


class CiLfsAssetTests(unittest.TestCase):
    @staticmethod
    def pointer(body):
        return ("version https://git-lfs.github.com/spec/v1\noid sha256:"
                + hashlib.sha256(body).hexdigest() + "\nsize " + str(len(body)) + "\n").encode()

    def test_canonical_pointer_and_existing_helm_cache_identity(self):
        pointer = self.pointer(b"fixture")
        with mock.patch.object(subprocess, "check_output", return_value=pointer) as git:
            record = identity.identify("helmholtz", ROOT)
        self.assertEqual(record["cache_key"], "arch-helmholtz-lfs-v1-" + hashlib.sha256(pointer).hexdigest())
        self.assertEqual(record["size"], 7)
        git.assert_called_once_with(["git", "show", "HEAD:EOS_toolkit/tables/helmholtz/helm_table.dat"], cwd=ROOT)

    def test_unknown_assets_and_noncanonical_pointers_reject(self):
        with self.assertRaises(ValueError):
            identity.identify("all-data")
        good = self.pointer(b"fixture")
        for pointer in (b"", good.replace(b"size 7", b"size 0"), good + b"extra\n",
                        good.replace(b"\n", b"\r\n"), good.replace(b"sha256:", b"sha256:Z")):
            with self.subTest(pointer=pointer), self.assertRaises(ValueError):
                identity.parse_pointer(pointer)

    def test_each_owner_has_a_distinct_exact_cache_key(self):
        pointer = self.pointer(b"fixture")
        with mock.patch.object(subprocess, "check_output", return_value=pointer):
            helm = identity.identify("helmholtz")
            studio = identity.identify("studio-sod")
        self.assertNotEqual(helm["cache_key"], studio["cache_key"])
        self.assertEqual(studio["path"], "studio/tests/fixtures/sod-1d.h5")

    def test_materialized_bytes_missing_pointer_size_and_hash_are_distinct(self):
        body = b"bounded unit fixture"
        record = {"path": "asset", **identity.parse_pointer(self.pointer(body))}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaisesRegex(ValueError, "regular file"):
                identity.verify(record, root)
            for contents, reason in ((self.pointer(body), "wrong size"), (body[:-1], "wrong size"),
                                     (b"X" * len(body), "SHA-256")):
                (root / "asset").write_bytes(contents)
                with self.subTest(reason=reason), self.assertRaisesRegex(ValueError, reason):
                    identity.verify(record, root)
            (root / "asset").write_bytes(body)
            self.assertEqual(identity.verify(record, root), record)

    def test_a_matching_symlink_is_not_materialized_asset_evidence(self):
        body = b"fixture"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "body").write_bytes(body)
            (root / "asset").symlink_to(root / "body")
            with self.assertRaisesRegex(ValueError, "regular file"):
                identity.verify({"path": "asset", **identity.parse_pointer(self.pointer(body))}, root)


if __name__ == "__main__":
    unittest.main()
