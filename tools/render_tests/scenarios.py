# Copyright (c) 2026, i2kiselev
# BSD 3-Clause License: see LICENSE in the repository root.
"""The scenarios and the tiers made of them. Each scenario is one launch of the game on the run's
state (purchases carry over between the scenarios of a run), and adds CheckResults.

Switches of the spec's list, and how each is covered:
- CAR DIRT: car-dirt (full dirt forced: thumbnail and garage must show it, and no seam).
- RESOLUTION SCALE: every tier runs 2x, long and full 3x, full 4x.
- --fh1_surfaceless_clears: surfaceless-clears (a counter: tile passes per frame).
- --fh1_async_readbacks: on in every run (a built-in default); its bugs show as tints (chroma).
- UPSCALER, RESOLUTION SCALE live, the window's size and fullscreen: window-upscalers.
- SHOWROOM MSAA: thumbnail-msaa-off (off: a bought car's thumbnail keeps its ground shadow, and
  the upgrade shop's part view its backdrop).
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
from .checks import (CheckResult, Figures, References, chroma_check, defaults_check,
                     depth_bands_per_frame, differ_check, hooks_hit, log_checks, max_check, min_check, ratio_check, reference_size,
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
# Project Forza Plus 4's races vary more: one build's Hockenheim transfers came 195 to 215 per
# frame, the stock game's 194 to 196.
PFP4_COUNTER_TOLERANCE = 0.15
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
# Inside the upgrade shop's part view (115,169 to 995,481 at 1280x720), right of 880x312 from
# the screen's corner: its dark backdrop (mean 2), or the flat grey (98) where its recorded
# command buffer was clipped to the view's size without the view's origin.
UPGRADE_BACKDROP = Rect(0.70, 0.25, 0.77, 0.42)
UPGRADE_BACKDROP_MAX = 30
# The seed's white Abarth's hood in the same view (mean 200): the view was drawn, so a dark
# backdrop is not a black frame.
UPGRADE_CAR = Rect(0.46, 0.40, 0.56, 0.44)
UPGRADE_CAR_MIN = 120


def counter_tolerance(release_name):
    if release_name.startswith("Project Forza Plus 4"):
        return PFP4_COUNTER_TOLERANCE
    return COUNTER_TOLERANCE


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
    counter_tolerance: float = COUNTER_TOLERANCE

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
        run.add(figures.band(f"{label} resolves", window.resolves, run.counter_tolerance),
                figures.band(f"{label} transfers", window.transfers, run.counter_tolerance),
                figures.band(f"{label} transfer tile passes", window.transfer_tile_passes,
                             run.counter_tolerance),
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
    floated. The upgrade shop's part view keeps its dark backdrop (grey with a black box in its
    corner up to 0.3.0)."""
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
            routes.upgrade_part_view(session)
            time.sleep(6)
            image = session.capture("upgrade-part-view")
            run.add(max_check("upgrade part view backdrop", images.mean(image, UPGRADE_BACKDROP),
                              UPGRADE_BACKDROP_MAX))
            run.add(min_check("upgrade part view car", images.mean(image, UPGRADE_CAR),
                              UPGRADE_CAR_MIN))
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


# The layer's defaults (core and thread safety on, synchronization off); each scenario took about
# 2 minutes under them. Errors only: a warning on every draw rotates the log past everything else.
VALIDATION_ENV = {"VK_KHRONOS_VALIDATION_REPORT_FLAGS": "error",
                  "VK_KHRONOS_VALIDATION_DUPLICATE_MESSAGE_LIMIT": "0"}
_LAYER_INSERTED = 'Inserted device layer "VK_LAYER_KHRONOS_validation"'


def validation(run):
    """2x with the Vulkan validation layer: a race logs no validation error."""
    scale = 2
    with run.session(scale, ["--vulkan_validation_enabled=true"], env=VALIDATION_ENV) as session:
        try:
            routes.to_main_menu(session)
            after = _title_time(session)
            _drive_only(run, session, scale, "race validation")
            _log_checks(run, session, after)
            _layer_checks(run, session)
        finally:
            run.keep_log(session)


def _layer_checks(run, session):
    # Without this a run the layer never saw passes: errors reach the log only through it.
    loaded = any(_LAYER_INSERTED in line.text for line in session.lines)
    run.add(CheckResult("validation layer loaded", loaded, "yes" if loaded else "no",
                        "yes", "" if loaded else "install the Vulkan SDK"))


def _depth_bands_since(session, mark, timeout=90):
    """Two of the executor's counter lines since a switch: every 600 frames, so a slow GPU takes
    longer, and an older line would count frames from before the switch."""
    deadline = time.monotonic() + timeout
    while True:
        session.poll()
        bands = depth_bands_per_frame(session.lines[mark:])
        if bands or time.monotonic() >= deadline:
            return bands
        time.sleep(1)


def validation_upscaler(run):
    """1x under the validation layer, an upscaler turned on at run time in the main menu (FSR 3
    too) and in a hot lap: the scene keeps its MSAA tiles until the next is pushed, so camera
    depth is captured per tile.
    A set rewritten per tile once raced the submission worker's bind (the 161st's errors), though
    never on a launch with an upscaler, which has one tile. The layer only sees calls that overlap,
    so a clean run proves less than a failing one. The upscaler goes off before the race, whose
    scene is then pushed with its tiles."""
    scale = 1
    with run.session(scale, ["--vulkan_validation_enabled=true"], env=VALIDATION_ENV) as session:
        try:
            routes.to_main_menu(session)
            after = _title_time(session)
            session.poll()
            available = {match.group(1) for line in session.lines
                         for match in [_UPSCALER_AVAILABLE.search(line.text)] if match}
            # Any temporal upscaler tracks the camera; FSR 3 runs on every GPU.
            upscaler = "dlss" if "dlss" in available else "fsr3"
            mark = len(session.lines)
            session.set_option("present_effect", upscaler)
            time.sleep(25)
            run.add(min_check(f"validation {upscaler} on in the menu: depth bands a frame",
                              _depth_bands_since(session, mark), 2))
            if upscaler != "fsr3":
                # The FidelityFX context creates shaders that need the device's 16-bit features.
                mark = len(session.lines)
                session.set_option("present_effect", "fsr3")
                time.sleep(10)
                session.poll()
                ran = any(re.search(WINDOW_UPSCALER_RAN["fsr3"], line.text)
                          for line in session.lines[mark:])
                run.add(CheckResult("validation fsr3 on in the menu", ran,
                                    "context" if ran else "no context", "context"))
            session.set_option("present_effect", "bilinear")
            routes.quick_race(session, hot_lap=True)
            session.hold("A LX=0", 600)
            # A hot lap starts rolling.
            time.sleep(12)
            session.poll()
            mark = len(session.lines)
            session.set_option("present_effect", upscaler)
            time.sleep(25)
            run.add(min_check(f"validation {upscaler} on in a hot lap: depth bands a frame",
                              _depth_bands_since(session, mark), 2))
            # Not "cache warm": an upscaler's tiles make pipelines the shipped list may lack.
            run.add(*(result for result in log_checks(session.lines, after, WARM_PIPELINES)
                      if result.name != "cache warm"), defaults_check(session.lines))
            _layer_checks(run, session)
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


# The window's sizes in a window-upscalers pass, largest first: at 150 % display scale the first
# still holds the 2x render, so a pass crosses from an upscaler running to it standing aside.
WINDOW_SIZES = ((1800, 1012), (1600, 900), (1280, 720), (1024, 576), (640, 360))
WINDOW_UPSCALERS = ("bilinear", "fsr", "fsr2", "fsr3", "dlss")
# What a launch logs once the upscaler has really run for this render size.
WINDOW_UPSCALER_RAN = {"fsr2": r"FidelityFX: FSR 2\.\S+ context", "fsr3": r"FidelityFX: FSR 3\.\S+ context",
                       "dlss": r"DLSS: feature"}
_UPSCALER_AVAILABLE = re.compile(r"VulkanPresenter: upscaler (\w+) \(.*\) available")
# Below the top in a race: the timer and the OSD of a capture tool change between two captures.
# In the garage only its 3D scene: the menus around it are drawn right whatever the upscaler does.
WINDOW_FRAMING_VIEWS = {"garage": GARAGE_CAR, "hot lap": Rect(0, 0.2, 1, 1)}
# Measured on stills from races: the right framing 0.69 to 0.99 (a car on the grid moves a little,
# a small window aliases), FSR 2's scene at the old output size 0.07 to 0.14.
WINDOW_FRAMING_MIN = 0.5
# Sizes make no pipelines of their own, but DLSS Auto's preset M to K below a 1.8x upscale made 8
# the seed's cache lacks, in one pass: WARM_PIPELINES holds them.


def _set_window_size(session, size):
    session.set_option("window_width", size[0])
    session.set_option("window_height", size[1])


def _pipelines_created(session):
    session.poll()
    return sum(line.text.startswith("Creating graphics pipeline state") for line in session.lines)


def _window_framing(run, session, place, upscaler, scale):
    label = f"{place} {upscaler} {scale}x"
    session.set_option("present_effect", upscaler)
    _set_window_size(session, WINDOW_SIZES[0])
    time.sleep(4)
    first = session.capture_window(f"window {label} first")

    pipelines = _pipelines_created(session)
    render = (1280 * scale, 720 * scale)
    # DLSS needs the scene's depth and motion, which the garage's view lacks: it stands aside there
    # (FSR gets neutral inputs), so only a hot lap shows it running.
    expected_to_run = upscaler in WINDOW_UPSCALER_RAN and not (upscaler == "dlss" and place == "garage")
    if expected_to_run and first.width >= render[0] and first.height >= render[1]:
        session.poll()
        ran = re.compile(f"{WINDOW_UPSCALER_RAN[upscaler]} for {render[0]}x{render[1]} to ")
        run.add(CheckResult(f"window upscaler ran {label}",
                            any(ran.search(line.text) for line in session.lines),
                            "logged" if any(ran.search(line.text) for line in session.lines)
                            else "no context or feature", "logged"))

    def same_framing(step, size):
        image = session.capture_window(f"window {label} {step}")
        correlation = images.framing_correlation(first, image, WINDOW_FRAMING_VIEWS[place])
        run.add(min_check(f"window framing {label} {step}", round(correlation, 3),
                          WINDOW_FRAMING_MIN))
        # Without these a game that stopped would pass: its last picture has the same framing. A
        # guest capture is written from a frame in flight, and breaks the run if none comes.
        expected = (first.width * size[0] // WINDOW_SIZES[0][0],
                    first.height * size[1] // WINDOW_SIZES[0][1])
        run.add(CheckResult(f"window size {label} {step}",
                            abs(image.width - expected[0]) <= 2 and abs(image.height - expected[1]) <= 2,
                            image.size, f"{expected[0]}x{expected[1]}"))
        session.capture(f"window {label} {step} guest")

    for size in WINDOW_SIZES[1:]:
        _set_window_size(session, size)
        time.sleep(2.5)
        same_framing(f"{size[0]}x{size[1]}", size)
    _set_window_size(session, WINDOW_SIZES[0])
    time.sleep(2.5)
    same_framing("back to first", WINDOW_SIZES[0])
    session.set_option("fullscreen", "true")
    time.sleep(2.5)
    fullscreen = session.capture_window(f"window {label} fullscreen")
    run.add(CheckResult(f"window went fullscreen {label}", fullscreen.width > first.width,
                        fullscreen.size, f"wider than {first.size}"))
    session.set_option("fullscreen", "false")
    time.sleep(2.5)
    same_framing("out of fullscreen", WINDOW_SIZES[0])
    run.add(max_check(f"window pipelines {label}", _pipelines_created(session) - pipelines,
                      WARM_PIPELINES))


def window_upscalers(run):
    """Every upscaler at 1x and 2x, in the garage and in a race, which render differently: resized,
    and back from fullscreen (through the option: Alt+Enter itself takes real keys and the focus),
    the window shows the same framing and makes no pipelines. Both hold still: the garage has the showroom's rendering without the showroom's
    orbiting camera, and the race is a hot lap on the handbrake (the brake reverses, and on a grid an
    AI car may hit the player's). FSR 2 once kept a larger output's size: only the scene's top left
    corner showed."""
    # The scale is read when the renderer starts.
    for scale in (1, 2):
        with run.session(scale) as session:
            try:
                routes.to_main_menu(session)
                after = _title_time(session)
                routes.buy_car(session)
                routes.garage(session, from_main_menu=False)
                session.poll()
                available = {"bilinear", "fsr"} | {
                    match.group(1) for line in session.lines
                    for match in [_UPSCALER_AVAILABLE.search(line.text)] if match}
                run.add(CheckResult("window FSR 2 and 3 available", {"fsr2", "fsr3"} <= available,
                                    " ".join(sorted(available)), "both, on any GPU"))
                upscalers = [name for name in WINDOW_UPSCALERS if name in available]
                for upscaler in upscalers:
                    _window_framing(run, session, "garage", upscaler, scale)
                routes.back_to_main_menu(session)
                routes.quick_race(session, hot_lap=True)
                session.hold("A LX=0", 600)
                # A hot lap starts rolling.
                time.sleep(12)
                # A temporal upscaler runs only on an output at least the render's size: at 2x the
                # windows are smaller, and the first one to run in the race (FSR 2 going
                # fullscreen) built the race's upscaler variants there, 87 pipelines the seed lacks.
                # One run here leaves each window step what the window itself makes.
                session.set_option("present_effect", "fsr2")
                session.set_option("fullscreen", "true")
                time.sleep(4)
                session.set_option("fullscreen", "false")
                time.sleep(2.5)
                for upscaler in upscalers:
                    _window_framing(run, session, "hot lap", upscaler, scale)
                session.poll()
                # Not "cache warm": the first upscaler of a launch turns MSAA off, which makes
                # pipelines the shipped list may lack. _window_framing counts what the window does.
                run.add(*(result for result in log_checks(session.lines, after, WARM_PIPELINES)
                          if result.name != "cache warm"), defaults_check(session.lines))
            finally:
                run.keep_log(session)


SCENARIOS = {"short": short, "thumbnail-msaa-off": thumbnail_msaa_off,
             "thumbnail-late-streaming": thumbnail_late_streaming, "session-2x": session_2x,
             "session-3x": session_3x, "car-dirt": car_dirt,
             "surfaceless-clears": surfaceless_clears, "vram-4x": vram_4x,
             "validation": validation, "validation-upscaler": validation_upscaler,
             "cold-cache": cold_cache, "window-upscalers": window_upscalers}
TIERS = {
    "short": ["short", "thumbnail-msaa-off", "thumbnail-late-streaming"],
    "long": ["short", "thumbnail-msaa-off", "thumbnail-late-streaming", "session-2x",
             "session-3x"],
    # The cold cache empties the run's shader cache: it comes last.
    "full": ["short", "thumbnail-msaa-off", "thumbnail-late-streaming", "session-2x",
             "session-3x", "car-dirt", "surfaceless-clears", "vram-4x", "validation",
             "validation-upscaler", "window-upscalers", "cold-cache"],
}
