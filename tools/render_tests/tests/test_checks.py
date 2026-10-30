import re
import struct
import tempfile
import unittest
from datetime import datetime
from pathlib import Path

from render_tests import images, seed
from render_tests.checks import (References, band_check, chroma_check, defaults_check,
                                 differ_check, hook_census_check, hooks_hit, log_checks,
                                 max_check, min_check, pipeline_list_check)
from render_tests.images import Image, ImageLimits, Rect
from render_tests.logtail import parse


def solid(width, height, rgb):
    return Image(width, height, bytes(rgb) * (width * height))


def line(time, text, level="info"):
    return parse(f"[2026-10-24 {time}] [{level}] [gpu] [t1] {text}")


class ReferencesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / "references"
        self.captures = Path(self.temp.name) / "captures"
        self.captures.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def references(self, record=False, force=False):
        return References(self.root, 2, record=record, force=force, diff_dir=self.captures)

    def test_missing_reference_fails(self):
        result = self.references().image_check("showroom", solid(4, 4, (1, 2, 3)), ImageLimits())
        self.assertFalse(result.passed)
        self.assertIn("no reference", result.value)

    def test_record_writes_reference(self):
        result = self.references(record=True).image_check("showroom", solid(4, 4, (1, 2, 3)),
                                                          ImageLimits())
        self.assertTrue(result.passed)
        written = images.read_png(self.root / "2x" / "showroom.png")
        self.assertEqual(written.rgb, solid(4, 4, (1, 2, 3)).rgb)

    def test_record_keeps_references_at_1280_wide(self):
        self.references(record=True).image_check("race", solid(2560, 1440, (9, 9, 9)), ImageLimits())
        written = images.read_png(self.root / "2x" / "race.png")
        self.assertEqual((written.width, written.height), (1280, 720))

    def test_record_refuses_overwrite(self):
        self.references(record=True).image_check("showroom", solid(4, 4, (1, 2, 3)), ImageLimits())
        result = self.references(record=True).image_check("showroom", solid(4, 4, (200, 0, 0)),
                                                          ImageLimits())
        self.assertFalse(result.passed)
        self.assertEqual(images.read_png(self.root / "2x" / "showroom.png").rgb,
                         solid(4, 4, (1, 2, 3)).rgb)
        forced = self.references(record=True, force=True).image_check(
            "showroom", solid(4, 4, (200, 0, 0)), ImageLimits())
        self.assertTrue(forced.passed)
        self.assertEqual(images.read_png(self.root / "2x" / "showroom.png").rgb,
                         solid(4, 4, (200, 0, 0)).rgb)

    def test_failure_writes_diff(self):
        self.references(record=True).image_check("showroom", solid(8, 8, (0, 0, 0)), ImageLimits())
        result = self.references().image_check("showroom", solid(8, 8, (255, 255, 255)),
                                               ImageLimits())
        self.assertFalse(result.passed)
        self.assertTrue((self.captures / "showroom.diff.png").exists())

    def test_a_second_check_of_one_reference_has_its_own_name(self):
        self.references(record=True).image_check("garage", solid(8, 8, (0, 0, 0)), ImageLimits())
        result = self.references().image_check("garage", solid(8, 8, (0, 0, 0)), ImageLimits(),
                                               Rect(0, 0, 0.5, 0.5), label="garage nose")
        self.assertEqual(result.name, "image garage nose")
        self.assertTrue(result.passed)

    def test_size_mismatch_names_both_sizes(self):
        self.references(record=True).image_check("showroom", solid(8, 8, (0, 0, 0)), ImageLimits())
        result = self.references().image_check("showroom", solid(4, 4, (0, 0, 0)), ImageLimits())
        self.assertFalse(result.passed)
        self.assertIn("4x4", result.detail)
        self.assertIn("8x8", result.detail)


class ChecksTest(unittest.TestCase):
    def test_chroma_check_fails_on_purple(self):
        self.assertTrue(chroma_check("car", solid(8, 8, (240, 240, 238)), Rect(0, 0, 1, 1), 10, 20).passed)
        self.assertFalse(chroma_check("car", solid(8, 8, (235, 200, 245)), Rect(0, 0, 1, 1), 10, 20).passed)

    def test_chroma_check_fails_without_bright_pixels(self):
        result = chroma_check("car", solid(8, 8, (10, 10, 10)), Rect(0, 0, 1, 1), 10, 20)
        self.assertFalse(result.passed)
        self.assertIn("no bright pixels", result.detail)

    def test_tint_check_passes_shaded_white_and_fails_magenta(self):
        from render_tests.checks import tint_check
        self.assertTrue(tint_check("car", solid(8, 8, (200, 215, 240)), Rect(0, 0, 1, 1), 8).passed)
        self.assertFalse(tint_check("car", solid(8, 8, (240, 150, 240)), Rect(0, 0, 1, 1), 8).passed)

    def test_differ_check(self):
        dark, light = solid(8, 8, (0, 0, 0)), solid(8, 8, (60, 60, 60))
        self.assertTrue(differ_check("dirt", dark, light, Rect(0, 0, 1, 1), 20).passed)
        self.assertFalse(differ_check("dirt", dark, dark, Rect(0, 0, 1, 1), 20).passed)

    def test_band_and_max_checks(self):
        self.assertTrue(band_check("resolves", 29.4, 29.0, 0.02).passed)
        self.assertFalse(band_check("resolves", 30.0, 29.0, 0.02).passed)
        self.assertTrue(max_check("readbacks", 0.0, 0.0).passed)
        self.assertFalse(max_check("readbacks", 0.1, 0.0).passed)
        self.assertTrue(min_check("shadow", 1500, 1500).passed)
        self.assertFalse(min_check("shadow", 414, 1500).passed)

    def test_cache_warm_fails_on_pipeline_creation_after_title(self):
        after = datetime(2026, 10, 24, 1, 3, 34)
        early = [line("01:03:31.000", "Creating graphics pipeline state with VS 0000000000000001, PS 0000000000000002")]
        late = early + [line("01:05:00.000", "Creating graphics pipeline state with VS 0000000000000003, PS 0000000000000004")]
        warm = {result.name: result for result in log_checks(early, after)}
        cold = {result.name: result for result in log_checks(late, after)}
        self.assertTrue(warm["cache warm"].passed)
        self.assertFalse(cold["cache warm"].passed)
        self.assertEqual(cold["cache warm"].value, "1 pipelines created")

    def test_pipelines_of_shaders_the_disc_lacks_do_not_count(self):
        after = datetime(2026, 10, 24, 1, 3, 34)
        lines = [line("01:05:00.000", "Creating graphics pipeline state with VS 1E6883FCCDE1F688, "
                      "PS A4A965C189287B99"),
                 line("01:05:00.000", "Creating graphics pipeline state with VS B6C9863F710683EC"),
                 line("01:05:00.000", "Creating graphics pipeline state with VS 1E6883FCCDE1F688, "
                      "PS 0123456789ABCDEF")]
        results = {result.name: result for result in log_checks(lines, after)}
        self.assertEqual(results["cache warm"].value, "1 pipelines created")

    def test_cache_warm_allows_a_few_pipelines_the_shipped_list_lacks(self):
        after = datetime(2026, 10, 24, 1, 3, 34)
        lines = [line("01:05:00.000", "Creating graphics pipeline state with VS 8D05A46C3584CA9E, "
                      "PS C5862561EE9EC6CB")] * 3
        self.assertTrue({r.name: r for r in log_checks(lines, after, 3)}["cache warm"].passed)
        self.assertFalse({r.name: r for r in log_checks(lines, after, 2)}["cache warm"].passed)

    def test_validation_errors_fail(self):
        after = datetime(2026, 10, 24, 1, 0, 0)
        lines = [line("01:05:00.000", "Vulkan Error (Validation, ID 1: UNASSIGNED-Threading-"
                      "MultipleThreads): THREADING ERROR", level="error"),
                 line("01:05:00.000", "Vulkan Warning (Validation, ID 2): fragment output",
                      level="warning")]
        results = {result.name: result for result in log_checks(lines, after)}
        self.assertFalse(results["no validation errors"].passed)
        self.assertEqual(results["no validation errors"].value, "1 errors")
        self.assertTrue(results["no crash"].passed)


if __name__ == "__main__":
    unittest.main()


class FiguresTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def test_missing_figure_fails_unless_recording(self):
        from render_tests.checks import Figures
        self.assertFalse(Figures(self.root, 2).band("race resolves", 29.0, 0.05).passed)
        recorded = Figures(self.root, 2, record=True).band("race resolves", 29.0, 0.05)
        self.assertTrue(recorded.passed)
        self.assertEqual(recorded.value, "recorded 29.0")

    def test_band_and_ceiling_against_recorded_figures(self):
        from render_tests.checks import Figures
        Figures(self.root, 2, record=True).band("race resolves", 29.0, 0.05)
        Figures(self.root, 2, record=True).ceiling("race vram", 3000.0, 0.15)
        figures = Figures(self.root, 2)
        self.assertTrue(figures.band("race resolves", 30.0, 0.05).passed)
        self.assertFalse(figures.band("race resolves", 31.0, 0.05).passed)
        self.assertTrue(figures.ceiling("race vram", 3400.0, 0.15).passed)
        self.assertFalse(figures.ceiling("race vram", 3500.0, 0.15).passed)

    def test_get_reads_a_recorded_figure(self):
        from render_tests.checks import Figures
        self.assertIsNone(Figures(self.root, 2).get("race resolves"))
        Figures(self.root, 2, record=True).band("race resolves", 29.0, 0.05)
        self.assertEqual(Figures(self.root, 2).get("race resolves"), 29.0)

    def test_record_keeps_an_existing_figure_without_force(self):
        from render_tests.checks import Figures
        Figures(self.root, 2, record=True).band("race resolves", 29.0, 0.05)
        self.assertFalse(Figures(self.root, 2, record=True).band("race resolves", 40.0, 0.05).passed)
        self.assertTrue(Figures(self.root, 2, record=True, force=True).band("race resolves", 40.0, 0.05).passed)
        self.assertTrue(Figures(self.root, 2).band("race resolves", 40.0, 0.05).passed)


class RatioTest(unittest.TestCase):
    def test_ratio_check(self):
        from render_tests.checks import ratio_check
        self.assertTrue(ratio_check("tile passes off/on", 7600.0, 1550.0, 2.0).passed)
        self.assertFalse(ratio_check("tile passes off/on", 1560.0, 1550.0, 2.0).passed)
        self.assertFalse(ratio_check("tile passes off/on", 1560.0, None, 2.0).passed)


class DefaultsTest(unittest.TestCase):
    def test_no_refused_default_passes(self):
        result = defaults_check([line("01:03:00.000", "Built-in default: gpu_backend = vulkan")])
        self.assertTrue(result.passed)
        self.assertEqual(result.value, "0 refused")

    def test_a_refused_default_fails_naming_it(self):
        result = defaults_check([line("01:03:00.000", "Built-in default: gpu_backend = vulkan"),
                                 line("01:03:00.000", "Built-in default: fh1_fold_clears refused false",
                                      level="error")])
        self.assertFalse(result.passed)
        self.assertEqual(result.value, "1 refused")
        self.assertIn("fh1_fold_clears", result.detail)


# The cache writer's version, which the shipped list and the SDK must both carry.
TOOL_VERSION = int(re.search(r"kPipelineStorageVersion = (0x[0-9A-Fa-f]+)",
                             (seed.PROJECT / "src" / "shader_cache" / "cache_writer.h")
                             .read_text(encoding="utf-8")).group(1), 16)


class PipelineListTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.sdk = Path(self.temp.name) / "sdk"
        for relative, text in (("include/rex/graphics/vulkan/pipeline_cache.h",
                                "REXPACKEDSTRUCT(PipelineDescription, {\n"
                                f"  static constexpr uint32_t kVersion = {TOOL_VERSION:#x};\n"),
                               ("include/rex/graphics/pipeline/shader/spirv_translator.h",
                                "union Modification {\n"
                                "  static constexpr uint32_t kVersion = 0x20201219;\n")):
            (self.sdk / relative).parent.mkdir(parents=True, exist_ok=True)
            (self.sdk / relative).write_text(text, encoding="utf-8")

    def tearDown(self):
        self.temp.cleanup()

    def xpso(self, version):
        path = Path(self.temp.name) / "list.xpso"
        path.write_bytes(struct.pack("<I", 0x53504558) + bytes(4) + struct.pack(">I", version))
        return path

    def test_matching_versions_pass(self):
        result = pipeline_list_check(self.sdk, self.xpso(TOOL_VERSION))
        self.assertTrue(result.passed, result.detail)
        self.assertEqual(result.value, "ok")

    def test_a_version_mismatch_fails(self):
        result = pipeline_list_check(self.sdk, self.xpso(TOOL_VERSION - 1))
        self.assertFalse(result.passed)
        self.assertIn("mismatch", result.value)

    def test_a_missing_sdk_tree_fails_the_check(self):
        missing = Path(self.temp.name) / "no-sdk"
        result = pipeline_list_check(missing, self.xpso(TOOL_VERSION))
        self.assertFalse(result.passed)
        self.assertIn(str(missing), result.detail)


class HookCensusTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "hook-census.json"

    def tearDown(self):
        self.temp.cleanup()

    def test_reads_the_census_lines(self):
        lines = [line("10:00:00.000", "Hook census: Fm4MainLoopStep"),
                 line("10:00:00.001", "Hook census: Fm4LoadingScreen"),
                 line("10:00:00.002", "String: Hook census: Fm4Fake")]
        self.assertEqual(hooks_hit(lines), {"Fm4MainLoopStep", "Fm4LoadingScreen"})

    def test_records_then_compares(self):
        recorded = hook_census_check(self.path, "full", {"A", "B"}, record=True)
        self.assertTrue(recorded.passed)
        self.assertTrue(hook_census_check(self.path, "full", {"A", "B"}).passed)
        extra = hook_census_check(self.path, "full", {"A", "B", "C"})
        self.assertTrue(extra.passed)
        self.assertIn("new C", extra.detail)

    def test_a_missing_hook_fails(self):
        hook_census_check(self.path, "full", {"A", "B"}, record=True)
        result = hook_census_check(self.path, "full", {"A"})
        self.assertFalse(result.passed)
        self.assertIn("missing B", result.detail)

    def test_another_tier_has_no_reference(self):
        hook_census_check(self.path, "full", {"A"}, record=True)
        self.assertFalse(hook_census_check(self.path, "short", {"A"}).passed)
        hook_census_check(self.path, "short", {"A", "B"}, record=True)
        self.assertTrue(hook_census_check(self.path, "full", {"A"}).passed)
