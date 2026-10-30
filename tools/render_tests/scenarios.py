"""The scenarios and the tiers made of them. Each scenario is one launch of the game on the run's
state (purchases carry over between the scenarios of a run), and adds CheckResults.

Switches of the spec's list, and how each is covered:
- CAR DIRT: car-dirt (full dirt forced: thumbnail and garage must show it, and no seam).
- RESOLUTION SCALE: every tier runs 2x, long and full 3x, full 4x.
- --fh1_surfaceless_clears: surfaceless-clears (a counter: tile passes per frame).
- --fh1_async_readbacks: on in every run (a built-in default); its bugs show as tints (chroma).
- SHOWROOM MSAA: thumbnail-msaa-off (off: a bought car's thumbnail keeps its ground shadow).
  Every other run keeps the console's 4x, which the references were captured with.
Left out, and why:
- RACE MSAA, MOTION BLUR, DEPTH OF FIELD,
  --fm4_full_rate_scenes, --fm4_road_detail_blur, the presets: their effect shows in races, on
  crowds, fences and the road in motion, and a race has no fixed camera (the grid slot and the
  AI cars change every run); the showroom does not use them.
"""
import re
import shutil
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

from . import counters, images, routes, seed, thumbnails
from .checks import (CheckResult, Figures, References, chroma_check, defaults_check, differ_check,
                     hooks_hit, log_checks, max_check, min_check, ratio_check, reference_size,
                     tint_check)
from .game import Session
from .images import ImageLimits, Rect

BASE_ARGS = ["--fm4_skip_intro=true", "--audio_mute=true", "--fullscreen=false",
             "--window_width=1280", "--window_height=720", "--fm4_log_file_opens=true",
             "--fm4_log_strings=true", "--fm4_hook_census=true"]

# The white Abarth's paint in each fixed view, measured on the reference captures.
THUMBNAIL_CAR = Rect(0.12, 0.25, 0.65, 0.80)
GARAGE_CAR = Rect(0.24, 0.30, 0.56, 0.60)
SHOWROOM_CAR = Rect(0.43, 0.30, 0.86, 0.72)
RACE_CAR = Rect(0.39, 0.50, 0.61, 0.88)
CHROMA_MEAN = 12
CHROMA_P95 = 30
# Garage views, thumbnails and the main menu come out the same to the bit run after run; the
# showroom camera drifts a little (up to mean 1.8, 1.7 % of pixels, measured over three runs).
FIXED = ImageLimits(1.0, 3.0, 0.001)
SHOWROOM = ImageLimits(4.0, 12.0, 0.05)
# A thumbnail's small side view at its right edge came out a little different once in four 3x
# runs (0.12 % of the pixels, the main view the same to the bit); a 64x64 block is 1.85 %.
THUMBNAIL = ImageLimits(1.0, 3.0, 0.005)
# The bottom bar of the showroom views animates (the colour swatches): compared above it.
SHOWROOM_VIEW = Rect(0, 0, 1, 0.88)
# Per-frame counters of the same race came within 1 % over four runs.
COUNTER_TOLERANCE = 0.03
# Sky-blue shade on the white paint is not a bug: in races only green below red and blue is.
RACE_TINT = 4
# Pipelines a warm cache may still make for pairs the shipped list lacks (the AI cars vary).
WARM_PIPELINES = 10
VRAM_MARGIN = 0.15
# Forced full dirt against clean, measured over the car areas: garage 5.8, thumbnail 4.0.
DIRT_MIN_DIFFERENCE = 3.0
DIRT_MIN_DIFFERENCE_THUMBNAIL = 2.0
# The garage view's hood, badge to windscreen.
GARAGE_NOSE = Rect(0.44, 0.39, 0.57, 0.56)
# A race on the reference build reads back a few resolves once now and then (up to 0.07 a frame
# over a 45 s drive into the walls; race-perf.ps1 prints 0.02-0.04 as 0.0). A race that reads
# back every frame, the regression this guards, gives 1 or more.
ONE_OFF_READBACKS = 0.25
# Coverage under the car in a thumbnail's A plane (thumbnails.shadow_pixels): 2,652 to 7,020 with
# the ground shadow, 337 to 414 without it, as SHOWROOM MSAA off made them from v0.2.0 on.
SHADOW_MIN_PIXELS = 1500


@dataclass
class Run:
    dir: Path
    seed: Path
    references: Path
    record: bool = False
    force: bool = False
    results: list = field(default_factory=list)
    launches: int = 0
    hooks: set = field(default_factory=set)

    def refs(self, scale):
        return References(self.references, scale, self.record, self.force, self.dir / "captures")

    def figures(self, scale):
        return Figures(self.references, scale, self.record, self.force)

    def session(self, scale, extra=(), env=None):
        self.launches += 1
        return Session(self.dir, BASE_ARGS + [f"--resolution_scale={scale}", *extra], env=env)

    def keep_log(self, session):
        session.poll()
        self.hooks |= hooks_hit(session.lines)
        log = session.state / "logs" / "runtime.log"
        if log.exists():
            shutil.copy2(log, self.dir / f"runtime-{self.launches}.log")

    def add(self, *results):
        for result in results:
            self.results.append(result)
            print(f"  {'PASS' if result.passed else 'FAIL'} {result.name}: {result.value} "
                  f"(limit {result.limit})", flush=True)


def _title_time(session):
    session.poll()
    return next(line.time for line in session.lines if line.text == routes.MAIN_MENU.line)


def _image(run, session, scale, name, rect=None, limits=FIXED, mask=None):
    image = session.capture(name)
    run.add(run.refs(scale).image_check(name, image, limits, mask))
    if rect:
        run.add(chroma_check(name, image, rect, CHROMA_MEAN, CHROMA_P95))
    return image


def _shadow(run, path, slot):
    shadow = thumbnails.shadow_pixels(thumbnails.read_thumbnail_alpha(path))
    run.add(min_check(f"thumbnail {slot} ground shadow pixels", shadow, SHADOW_MIN_PIXELS))


def _thumbnail(run, session, scale, slot):
    path = thumbnails.thumbnail_paths(session.state)[slot]
    image = thumbnails.read_thumbnail(path)
    run.add(run.refs(scale).image_check("thumbnail-abarth-500", image, THUMBNAIL))
    run.add(chroma_check(f"thumbnail {slot}", image, THUMBNAIL_CAR, CHROMA_MEAN, CHROMA_P95))
    _shadow(run, path, slot)


def _purchase(run, session, scale, garage_too):
    slot = routes.buy_car(session)
    _thumbnail(run, session, scale, slot)
    if garage_too:
        routes.dealer_showroom(session)
        time.sleep(3)
        _image(run, session, scale, "dealer-showroom", SHOWROOM_CAR, SHOWROOM, SHOWROOM_VIEW)
        routes.leave_dealer_showroom(session)
        routes.garage(session, from_main_menu=False)
        time.sleep(3)
        _image(run, session, scale, "garage", GARAGE_CAR)
    routes.back_to_main_menu(session)
    return slot


def _race_counters(session, start, end):
    time.sleep(6)  # the stats line that closes the window
    session.poll()
    return counters.window(session.lines, start, end)


def _race(run, session, scale, steps, seconds, label, environment=None):
    routes.quick_race(session, steps, environment)
    session.capture(f"{label}-start")
    start = datetime.now()
    routes.drive(session, seconds)
    end = datetime.now()
    image = session.capture(f"{label}-drive")
    run.add(tint_check(f"{label} car", image, RACE_CAR, RACE_TINT))
    vram = counters.vram_mb(session.pid)
    run.add(run.figures(scale).ceiling(f"{label} vram mb", vram, VRAM_MARGIN))
    window = _race_counters(session, start, end)
    if window is None:
        run.add(CheckResult(f"{label} counters", False, "no stats window", "2 stats lines"))
    else:
        figures = run.figures(scale)
        run.add(figures.band(f"{label} resolves", window.resolves, COUNTER_TOLERANCE),
                figures.band(f"{label} transfers", window.transfers, COUNTER_TOLERANCE),
                figures.band(f"{label} transfer tile passes", window.transfer_tile_passes,
                             COUNTER_TOLERANCE),
                max_check(f"{label} one-off readbacks", window.one_off_readbacks,
                          ONE_OFF_READBACKS))
    routes.quit_race(session)
    return vram


def _log_checks(run, session, after):
    session.poll()
    run.add(*log_checks(session.lines, after, WARM_PIPELINES), defaults_check(session.lines))


def short(run):
    """2x: the main menu, a purchase with its thumbnail, the dealer's showroom after it and the
    garage, the Free Play showroom, a race at Bernese Alps."""
    scale = 2
    with run.session(scale) as session:
        try:
            routes.to_main_menu(session)
            after = _title_time(session)
            _image(run, session, scale, "main-menu")
            _purchase(run, session, scale, garage_too=True)
            routes.showroom(session)
            time.sleep(4)
            _image(run, session, scale, "showroom", SHOWROOM_CAR, SHOWROOM, SHOWROOM_VIEW)
            routes.back_to_main_menu(session)
            _race(run, session, scale, 0, 30, "race bernese alps")
            _log_checks(run, session, after)
        finally:
            run.keep_log(session)


def thumbnail_late_streaming(run):
    """2x, the studio tracks' streaming thread (start 82926250) held back 500 ms: the bought car's
    thumbnail is lit as always. The final render went ahead with the car lit from captures of the
    studio before the thread had filled it in, a flat white car (about 1 launch in 10 by itself;
    every launch so held back)."""
    scale = 2
    with run.session(scale, ["--debug_guest_thread_start_delay=82926250:500"]) as session:
        try:
            routes.to_main_menu(session)
            after = _title_time(session)
            slot = routes.buy_car(session)
            path = thumbnails.thumbnail_paths(session.state)[slot]
            run.add(run.refs(scale).image_check("thumbnail-abarth-500",
                                                thumbnails.read_thumbnail(path), THUMBNAIL,
                                                label=f"thumbnail {slot} late streaming"))
            routes.back_to_main_menu(session)
            _log_checks(run, session, after)
        finally:
            run.keep_log(session)


def thumbnail_msaa_off(run):
    """2x, SHOWROOM MSAA off (as a temporal upscaler also makes it): the bought car's thumbnail
    keeps its ground shadow. From v0.2.0 the row set the thumbnail's own tiling too, and the car
    floated."""
    scale = 2
    with run.session(scale, ["--fm4_showroom_msaa=off"]) as session:
        try:
            routes.to_main_menu(session)
            after = _title_time(session)
            slot = routes.buy_car(session)
            path = thumbnails.thumbnail_paths(session.state)[slot]
            run.add(chroma_check(f"thumbnail {slot}", thumbnails.read_thumbnail(path),
                                 THUMBNAIL_CAR, CHROMA_MEAN, CHROMA_P95))
            _shadow(run, path, slot)
            routes.back_to_main_menu(session)
            # The seed's cache was warmed at the console's 4x: the showroom's single-sample
            # pipelines are made here (103 in a run), so only the rest of the log checks apply.
            session.poll()
            run.add(*[result for result in log_checks(session.lines, after)
                      if result.name != "cache warm"], defaults_check(session.lines))
        finally:
            run.keep_log(session)


# Environment select steps from Bernese Alps, its first entry, and the environments they reach.
TRACKS = [(1, "Camino Viejo de Montserrat", "race montserrat"),
          (4, "Hockenheimring", "race hockenheim"),
          (17, "Road Atlanta", "race road atlanta")]


def session_2x(run):
    """2x: three more races on other tracks, a purchase after each."""
    scale = 2
    with run.session(scale) as session:
        try:
            routes.to_main_menu(session)
            after = _title_time(session)
            for steps, environment, label in TRACKS:
                _race(run, session, scale, steps, 45, label, environment)
                _purchase(run, session, scale, garage_too=False)
            _log_checks(run, session, after)
        finally:
            run.keep_log(session)


def session_3x(run):
    """3x: two races, a purchase, the garage, the showroom."""
    scale = 3
    with run.session(scale) as session:
        try:
            routes.to_main_menu(session)
            after = _title_time(session)
            _race(run, session, scale, 0, 45, "race bernese alps")
            _race(run, session, scale, 17, 45, "race road atlanta", "Road Atlanta")
            _purchase(run, session, scale, garage_too=True)
            routes.showroom(session)
            time.sleep(4)
            _image(run, session, scale, "showroom", SHOWROOM_CAR, SHOWROOM, SHOWROOM_VIEW)
            routes.back_to_main_menu(session)
            _log_checks(run, session, after)
        finally:
            run.keep_log(session)


def dirt_pixel_shaders():
    """The dirt pixel shaders, read from src/car_dirt.cpp (kDirtPixelShaders)."""
    source = (seed.PROJECT / "src" / "car_dirt.cpp").read_text(encoding="utf-8")
    block = re.search(r"kDirtPixelShaders = \{(.*?)\};", source, re.DOTALL).group(1)
    return re.findall(r'"([0-9A-F]{16})"', block)


def _dirt_differs(run, scale, name, dirty, clean_name, rect, min_difference):
    clean = run.refs(scale).path(clean_name)
    if not clean.exists():
        run.add(CheckResult(f"differ {name}", False, "-", f"no {clean_name} reference", str(clean)))
        return
    run.add(differ_check(name, reference_size(dirty), images.read_png(clean), rect,
                         min_difference))


def car_dirt(run):
    """2x, the dirt forced to full on every dirt shader, then the Abarth bought: its thumbnail and
    the garage view (both the same to the bit run after run) must show the dirt, differing from
    the clean ones, and match their own references, the garage also over the nose alone, where
    the sixty-third session's seam ran: thin lines there are too few pixels for the whole view."""
    scale = 2
    override = ";".join(f"{shader}:c57.y=1" for shader in dirt_pixel_shaders())
    with run.session(scale, [f"--gpu_pixel_shader_constant_override={override}"]) as session:
        try:
            routes.to_main_menu(session)
            after = _title_time(session)
            slot = routes.buy_car(session)
            thumbnail = thumbnails.read_thumbnail(thumbnails.thumbnail_paths(session.state)[slot])
            refs = run.refs(scale)
            run.add(refs.image_check("thumbnail-full-dirt", thumbnail, THUMBNAIL))
            _dirt_differs(run, scale, "thumbnail dirt", thumbnail, "thumbnail-abarth-500",
                          THUMBNAIL_CAR, DIRT_MIN_DIFFERENCE_THUMBNAIL)
            routes.garage(session, from_main_menu=False)
            time.sleep(3)
            garage = _image(run, session, scale, "garage-full-dirt")
            run.add(refs.image_check("garage-full-dirt", garage, FIXED, GARAGE_NOSE,
                                     label="garage-full-dirt nose"))
            _dirt_differs(run, scale, "garage dirt", garage, "garage", GARAGE_CAR,
                          DIRT_MIN_DIFFERENCE)
            routes.back_to_main_menu(session)
            _log_checks(run, session, after)
        finally:
            run.keep_log(session)


def _drive_only(run, session, scale, label, seconds=30):
    """A Bernese Alps race whose checks are the scenario's own: returns its counters."""
    routes.quick_race(session, 0)
    start = datetime.now()
    routes.drive(session, seconds)
    end = datetime.now()
    image = session.capture(f"{label}-drive")
    run.add(tint_check(f"{label} car", image, RACE_CAR, RACE_TINT))
    window = _race_counters(session, start, end)
    routes.quit_race(session)
    if window is None:
        run.add(CheckResult(f"{label} counters", False, "no stats window", "2 stats lines"))
    return window


def surfaceless_clears(run):
    """2x, --fh1_surfaceless_clears=false (read when the renderer starts): the transfers' tile
    passes per frame must at least double against the short tier's race with it on (the
    twenty-fourth session: 7,600 against 1,550 at 3x)."""
    scale = 2
    with run.session(scale, ["--fh1_surfaceless_clears=false"]) as session:
        try:
            routes.to_main_menu(session)
            window = _drive_only(run, session, scale, "race surfaceless clears off")
            if window:
                baseline = run.figures(scale).get("race bernese alps transfer tile passes")
                run.add(ratio_check("surfaceless clears tile passes off/on",
                                    window.transfer_tile_passes, baseline, 2.0))
        finally:
            run.keep_log(session)


def vram_4x(run):
    """4x: three Bernese Alps races, each one's VRAM against its recorded figure, and the last
    no more than VRAM_MARGIN above the first (the fiftieth session's leak kept outgrown transfer
    buffers race after race)."""
    scale = 4
    with run.session(scale) as session:
        try:
            routes.to_main_menu(session)
            after = _title_time(session)
            vram = [_race(run, session, scale, 0, 30, label)
                    for label in ("race bernese alps", "race bernese alps 2", "race bernese alps 3")]
            run.add(max_check("vram growth over 3 races at 4x", vram[-1] / vram[0],
                              1 + VRAM_MARGIN))
            _log_checks(run, session, after)
        finally:
            run.keep_log(session)


# The fifty-fourth session's settings: only the layer's thread safety, which caught the AMD
# fence crash; the whole layer makes a race unplayably slow.
VALIDATION_ENV = {f"VK_KHRONOS_VALIDATION_{name}": "false" for name in (
    "VALIDATE_CORE", "VALIDATE_SYNC", "CHECK_SHADERS", "STATELESS_PARAM", "OBJECT_LIFETIME",
    "HANDLE_WRAPPING")}
VALIDATION_ENV["VK_KHRONOS_VALIDATION_THREAD_SAFETY"] = "true"
VALIDATION_ENV["VK_KHRONOS_VALIDATION_DUPLICATE_MESSAGE_LIMIT"] = "0"
_LAYER_INSERTED = 'Inserted device layer "VK_LAYER_KHRONOS_validation"'


def validation(run):
    """2x with the Vulkan validation layer's thread safety: a race logs no validation error."""
    scale = 2
    layer_log = run.dir / "validation-layer.log"
    env = dict(VALIDATION_ENV, VK_KHRONOS_VALIDATION_LOG_FILENAME=str(layer_log))
    with run.session(scale, ["--vulkan_validation_enabled=true"], env=env) as session:
        try:
            routes.to_main_menu(session)
            after = _title_time(session)
            _drive_only(run, session, scale, "race validation")
            _log_checks(run, session, after)
            loaded = any(_LAYER_INSERTED in line.text for line in session.lines)
            run.add(CheckResult("validation layer loaded", loaded, "yes" if loaded else "no",
                                "yes", "" if loaded else "install the Vulkan SDK"))
            errors = 0
            if layer_log.exists():
                errors = layer_log.read_text(encoding="utf-8", errors="replace").count(
                    "Validation Error")
            run.add(CheckResult("validation layer log", errors == 0, f"{errors} errors", "0",
                                str(layer_log)))
        finally:
            run.keep_log(session)


def cold_cache(run):
    """2x on an emptied shader cache: pipelines are created (the run is really cold), the
    showroom looks as on the warm cache (its reference) and the car is still white in the race.
    Races have no fixed camera, and the road at GO is 22-25 or 32-36 luma by grid slot (sun or
    shade), so the thirty-ninth session's dark asphalt is not measured in a race."""
    scale = 2
    shutil.rmtree(run.dir / "state" / "cache" / "shaders", ignore_errors=True)
    with run.session(scale) as session:
        try:
            routes.to_main_menu(session)
            after = _title_time(session)
            routes.showroom(session)
            time.sleep(4)
            image = session.capture("showroom-cold-cache")
            run.add(run.refs(scale).image_check("showroom", image, SHOWROOM, SHOWROOM_VIEW,
                                                label="showroom on a cold cache"))
            routes.back_to_main_menu(session)
            _drive_only(run, session, scale, "race cold cache")
            session.poll()
            results = {result.name: result
                       for result in log_checks(session.lines, after, WARM_PIPELINES)}
            cache = results["cache warm"]
            run.add(CheckResult("cold cache creates pipelines", not cache.passed, cache.value,
                                f"> {WARM_PIPELINES}"), results["no crash"])
        finally:
            run.keep_log(session)


SCENARIOS = {"short": short, "thumbnail-msaa-off": thumbnail_msaa_off,
             "thumbnail-late-streaming": thumbnail_late_streaming, "session-2x": session_2x,
             "session-3x": session_3x, "car-dirt": car_dirt,
             "surfaceless-clears": surfaceless_clears, "vram-4x": vram_4x,
             "validation": validation, "cold-cache": cold_cache}
TIERS = {
    "short": ["short", "thumbnail-msaa-off", "thumbnail-late-streaming"],
    "long": ["short", "thumbnail-msaa-off", "thumbnail-late-streaming", "session-2x",
             "session-3x"],
    # The cold cache empties the run's shader cache: it comes last.
    "full": ["short", "thumbnail-msaa-off", "thumbnail-late-streaming", "session-2x",
             "session-3x", "car-dirt", "surfaceless-clears", "vram-4x", "validation",
             "cold-cache"],
}
