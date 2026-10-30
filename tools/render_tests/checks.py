"""Checks: each turns a measurement into a CheckResult (PASS/FAIL, the value against its limit)."""
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from . import images

REFERENCE_WIDTH = 1280
_PIPELINE = re.compile(r"^Creating graphics pipeline state with VS ([0-9A-F]{16})(?:, PS ([0-9A-F]{16}))?")
# The shaders the disc-built cache cannot hold (D3D's clear and two vertex shaders its table
# cannot express): their few pipelines are made in play even on a warm cache.
NOT_ON_THE_DISC = frozenset({"1E6883FCCDE1F688", "B6C9863F710683EC", "A4A965C189287B99"})
_VALIDATION_ERROR = re.compile(r"^Vulkan Error \(")
_REFUSED_DEFAULT = re.compile(r"^Built-in default: (\S+) refused")
_HOOK_HIT = re.compile(r"^Hook census: (\w+)$")
SHADER_LIST = Path(__file__).resolve().parents[1] / "shader_list.py"


@dataclass(frozen=True)
class CheckResult:
    name: str
    passed: bool
    value: str
    limit: str
    detail: str = ""


def reference_size(image):
    """References are kept 1280 wide: a capture at a scale is box-filtered down to it."""
    factor = image.width // REFERENCE_WIDTH
    if factor > 1 and factor * REFERENCE_WIDTH == image.width and image.height % factor == 0:
        return images.downscale(image, REFERENCE_WIDTH, image.height // factor)
    return image


class References:
    """Reference images under <root>\\<scale>x\\<name>.png. Missing ones fail unless recording;
    recording writes only missing ones, and replaces an existing one only with force."""

    def __init__(self, root, scale, record=False, force=False, diff_dir=None):
        self.folder = Path(root) / f"{scale}x"
        self.record = record
        self.force = force
        self.diff_dir = Path(diff_dir) if diff_dir else None

    def path(self, name):
        return self.folder / f"{name}.png"

    def image_check(self, name, actual, limits, mask=None, label=None):
        """label names the check when one reference is checked twice (e.g. under a mask)."""
        check = f"image {label or name}"
        actual = reference_size(actual)
        path = self.path(name)
        limit = f"mean {limits.mean_abs:g}, rmse {limits.rmse:g}, changed {limits.changed_ratio:g}"
        if self.record and (self.force or not path.exists()):
            self.folder.mkdir(parents=True, exist_ok=True)
            images.write_png(path, actual)
            return CheckResult(check, True, "recorded", limit, str(path))
        if not path.exists():
            return CheckResult(check, False, "no reference", limit, str(path))
        reference = images.read_png(path)
        comparison = images.compare(actual, reference, limits, mask)
        value = (f"mean {comparison.mean_abs:.2f}, rmse {comparison.rmse:.2f}, "
                 f"changed {comparison.changed_ratio:.4f}")
        if not comparison.passed and self.diff_dir and actual.size == reference.size:
            self.diff_dir.mkdir(parents=True, exist_ok=True)
            images.write_png(self.diff_dir / f"{name}.diff.png",
                             images.difference_image(actual, reference))
        return CheckResult(check, comparison.passed, value, limit, comparison.detail)


class Figures:
    """Reference numbers (counters, VRAM) in <root>/<scale>x/figures.json, recorded like the
    reference images: missing ones fail unless recording, kept unless forced."""

    def __init__(self, root, scale, record=False, force=False):
        self.path = Path(root) / f"{scale}x" / "figures.json"
        self.record = record
        self.force = force

    def _load(self):
        return json.loads(self.path.read_text(encoding="utf-8")) if self.path.exists() else {}

    def _check(self, name, value, limit_text, passes):
        figures = self._load()
        if self.record and (self.force or name not in figures):
            figures[name] = value
            self.path.parent.mkdir(parents=True, exist_ok=True)
            self.path.write_text(json.dumps(figures, indent=2, sort_keys=True), encoding="utf-8")
            return CheckResult(name, True, f"recorded {value:.1f}", "-")
        if name not in figures:
            return CheckResult(name, False, f"{value:.1f}", "no reference", str(self.path))
        expected = figures[name]
        return CheckResult(name, passes(expected), f"{value:.1f}", limit_text(expected))

    def get(self, name):
        return self._load().get(name)

    def band(self, name, value, tolerance):
        return self._check(name, value, lambda expected: f"{expected:.1f} +-{tolerance * 100:g}%",
                           lambda expected: abs(value - expected) <= tolerance * abs(expected))

    def ceiling(self, name, value, margin):
        return self._check(name, value, lambda expected: f"<= {expected * (1 + margin):.0f}",
                           lambda expected: value <= expected * (1 + margin))


def hooks_hit(lines):
    """The game code hooks a session reached (--fm4_hook_census)."""
    return {match.group(1) for line in lines if (match := _HOOK_HIT.match(line.text))}


def hook_census_check(path, key, hooks, record=False, force=False):
    """The hooks a run reached against those recorded for its tier on a 1.0 build: a code image
    whose port misses a hook's place never reaches it. Missing ones fail; new ones are listed."""
    path = Path(path)
    recorded = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
    if record and (force or key not in recorded):
        recorded[key] = sorted(hooks)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(recorded, indent=2, sort_keys=True), encoding="utf-8")
        return CheckResult("hook census", True, f"recorded {len(hooks)} hooks", "-", str(path))
    if key not in recorded:
        return CheckResult("hook census", False, f"{len(hooks)} hooks", "no reference", str(path))
    missing = sorted(set(recorded[key]) - hooks)
    extra = sorted(hooks - set(recorded[key]))
    detail = "; ".join(part for part in (missing and f"missing {', '.join(missing)}",
                                         extra and f"new {', '.join(extra)}") if part)
    return CheckResult("hook census", not missing, f"{len(hooks)} hooks",
                       f"the {len(recorded[key])} recorded", detail)


def chroma_check(name, image, rect, max_mean, max_p95):
    measured = images.chroma(image, rect)
    limit = f"mean <= {max_mean:g}, p95 <= {max_p95:g}"
    if not measured.pixels:
        return CheckResult(f"chroma {name}", False, "-", limit, "no bright pixels in the area")
    passed = measured.mean <= max_mean and measured.p95 <= max_p95
    return CheckResult(f"chroma {name}", passed,
                       f"mean {measured.mean:.1f}, p95 {measured.p95}", limit,
                       f"{measured.pixels} bright pixels")


def tint_check(name, image, rect, max_green_deficit):
    """The past tints (purple, magenta) on the white car where lighting varies (races): green
    below red and blue."""
    measured = images.chroma(image, rect)
    limit = f"green deficit <= {max_green_deficit:g}"
    if not measured.pixels:
        return CheckResult(f"tint {name}", False, "-", limit, "no bright pixels in the area")
    return CheckResult(f"tint {name}", measured.green_deficit <= max_green_deficit,
                       f"green deficit {measured.green_deficit:.1f}", limit,
                       f"{measured.pixels} bright pixels")


def differ_check(name, first, second, rect, min_mean_abs):
    """A switch's two settings must differ in the area it acts on."""
    comparison = images.compare(first, second, images.ImageLimits(255, 255, 1), rect)
    passed = comparison.passed and comparison.mean_abs >= min_mean_abs
    return CheckResult(f"differ {name}", passed, f"mean {comparison.mean_abs:.2f}",
                       f">= {min_mean_abs:g}", comparison.detail)


def band_check(name, value, expected, tolerance):
    passed = abs(value - expected) <= tolerance * abs(expected)
    return CheckResult(name, passed, f"{value:.1f}", f"{expected:g} +-{tolerance * 100:g}%")


def ratio_check(name, value, baseline, min_ratio):
    """A switch whose effect is a counter: its other setting must change it by min_ratio."""
    if not baseline:
        return CheckResult(name, False, f"{value:.1f}", "no baseline")
    ratio = value / baseline
    return CheckResult(name, ratio >= min_ratio, f"{ratio:.2f}x ({value:.1f} / {baseline:.1f})",
                       f">= {min_ratio:g}x")


def max_check(name, value, limit):
    return CheckResult(name, value <= limit, f"{value:.3g}", f"<= {limit:g}")


def min_check(name, value, limit):
    return CheckResult(name, value >= limit, f"{value:g}", f">= {limit:g}")


def log_checks(lines, after, allowed_pipelines=0):
    """The log after a time (the title screen: the shader cache is loaded before it). A warm
    cache may still make a few pipelines the shipped list lacks (pairs of the random AI cars);
    a cold one makes thousands."""
    late = [line for line in lines if line.time and line.time >= after]
    created = sum(1 for line in late for match in [_PIPELINE.match(line.text)]
                  if match and not {shader for shader in match.groups() if shader} <= NOT_ON_THE_DISC)
    errors = [line for line in late if _VALIDATION_ERROR.match(line.text)]
    critical = [line for line in late if line.level in ("critical", "fatal")]
    return [
        CheckResult("cache warm", created <= allowed_pipelines, f"{created} pipelines created",
                    f"<= {allowed_pipelines}"),
        CheckResult("no validation errors", not errors, f"{len(errors)} errors", "0",
                    errors[0].text[:200] if errors else ""),
        CheckResult("no crash", not critical, f"{len(critical)} critical lines", "0",
                    critical[0].text[:200] if critical else ""),
    ]


def defaults_check(lines):
    """Over the whole log: the built-in defaults are applied before the title screen."""
    refused = [line for line in lines if _REFUSED_DEFAULT.match(line.text)]
    return CheckResult("built-in defaults applied", not refused, f"{len(refused)} refused", "0",
                       refused[0].text if refused else "")


def pipeline_list_check(sdk_root, xpso):
    """The shipped pipeline list's version against the SDK's and the cache writer's."""
    name, limit = "pipeline list version", "SDK = tool = list"
    for path in (sdk_root, xpso):
        if not Path(path).exists():
            return CheckResult(name, False, "missing", limit, str(path))
    # shader_list.py imports xxhash from the user site, which -I drops.
    result = subprocess.run([sys.executable, "-E", "-P", str(SHADER_LIST), "check-sdk",
                             str(sdk_root), str(xpso)], capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    output = [text for text in (result.stdout + result.stderr).splitlines() if text.strip()]
    return CheckResult(name, result.returncode == 0, output[-1] if output else "no output", limit,
                       " | ".join(output))
