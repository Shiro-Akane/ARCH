"""Current doc links must fail visibly; frozen provenance is never rewritten."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from check_documentation import anchors, check_file, maintained


class DocumentationTests(unittest.TestCase):
    def test_current_and_historical_owners_are_distinct(self):
        for name in ("README.md", "ARCH_RELEASE_PLAN.md", "docs/Reference.md",
                     "studio/README.md", "src/api/docs/PreviewApi.md", "docs/development/ImplementationOwnership.md",
                     "docs/histroy/README.md", "docs/development/archive/README.md"):
            self.assertTrue(maintained(name), name)
        for name in ("AGENTS.md", "ARCH_MODERNIZATION_PLAN.md", "docs/histroy/notes.md",
                     "docs/histroy/science/README.md", "docs/development/archive/modernization/Original.md",
                     "validation/hydro/results/README.md", "studio/docs/archive/README.md",
                     "docs/development/BuildOld-20261001.md", "docs/development/modernization/README.md"):
            self.assertFalse(maintained(name), name)

    def test_heading_identity_preserves_unicode_and_duplicate_order(self):
        self.assertEqual(anchors("# Build\n## Build\n## 参数 `x1`（单位）\n<a id='explicit'></a>\n"),
                         {"build", "build-1", "参数-x1单位", "explicit"})

    def test_missing_file_and_fragment_are_errors_with_actual_source_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "target.md").write_text("# Available\n", encoding="utf-8")
            (root / "README.md").write_text("[ok](target.md#available) [bad](target.md#absent) [gone](gone.md)", encoding="utf-8")
            count, errors = check_file(root, "README.md")
            self.assertEqual(count, 3)
            self.assertEqual(len(errors), 2)
            self.assertTrue(all(error.startswith("README.md:") for error in errors))

    def test_fenced_examples_external_links_and_encoded_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "target name.md").write_text("# 参数\n", encoding="utf-8")
            (root / "README.md").write_text(
                "[web](https://example.invalid/page)\n```md\n[example](missing.md)\n```\n"
                "[valid](<target%20name.md#%E5%8F%82%E6%95%B0>)\n", encoding="utf-8")
            self.assertEqual(check_file(root, "README.md"), (1, []))

    def test_repository_escape_is_rejected_without_reading_the_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "README.md").write_text("[escape](../outside.md)")
            count, errors = check_file(root, "README.md")
            self.assertEqual(count, 0)
            self.assertIn("escapes repository", errors[0])


if __name__ == "__main__":
    unittest.main()
