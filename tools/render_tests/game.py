"""Driving the game: launch on a run folder, scripted presses, waits on what the game logs,
captures, stop.

Presses go through the scripted-input file (src/scripted_input.h): lines
"<start s> <hold s> <control>" counted from the game's process start, re-read by the game when
the file changes. A capture is requested by writing "<script>.shot" holding the BMP's path.
"""
import ctypes
import json
import os
import re
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

from . import images, seed
from .logtail import LogTail, draws_per_frame, stats_lines

TOOLS = Path(__file__).resolve().parent.parent
POLL_SECONDS = 0.25
PRESS_LEAD_SECONDS = 0.3


class RunBroken(Exception):
    """The run itself broke: a wait timed out, the game exited, a capture never came."""

    def __init__(self, step, reason):
        super().__init__(f"{step}: {reason}")
        self.step = step
        self.reason = reason


class Signal:
    def matches(self, line):
        return False

    def ready(self, session, lines):
        return any(self.matches(line) for line in lines)


class Log(Signal):
    def __init__(self, pattern):
        self.pattern = re.compile(pattern)

    def matches(self, line):
        return bool(self.pattern.search(line.text))

    def __repr__(self):
        return f"log line /{self.pattern.pattern}/"


class FileOpened(Signal):
    """A "File open:" line (--fm4_log_file_opens) naming a path that holds the substring."""

    def __init__(self, substring):
        self.substring = substring.lower()

    def matches(self, line):
        return line.text.startswith("File open: ") and self.substring in line.text.lower()

    def __repr__(self):
        return f"file open {self.substring!r}"


class String(Signal):
    """A "String:" line (--fm4_log_strings): the game looked up this exact text, e.g. a screen's
    title as the screen opens."""

    def __init__(self, text):
        self.line = f"String: {text}"

    def matches(self, line):
        return line.text == self.line

    def __repr__(self):
        return repr(self.line)


class Event(Signal):
    """An input event mark (MarkInputEvent)."""

    def __init__(self, name):
        self.name = name
        self.prefix = f"Input event {name} at "

    def matches(self, line):
        return line.text.startswith(self.prefix)

    def __repr__(self):
        return f"input event {self.name}"


class HostFile(Signal):
    """A file the game writes, newer than a time (seconds since the epoch)."""

    def __init__(self, path, newer_than=0.0):
        self.path = Path(path)
        self.newer_than = newer_than

    def ready(self, session, lines):
        try:
            return self.path.stat().st_mtime > self.newer_than
        except FileNotFoundError:
            return False

    def __repr__(self):
        return f"file {self.path}"


class DrawsAtLeast(Signal):
    """Draws per frame between the last two executor stats lines, the later one new."""

    def __init__(self, draws):
        self.draws = draws

    def ready(self, session, lines):
        return bool(stats_lines(lines)) and draws_per_frame(session.lines) >= self.draws

    def __repr__(self):
        return f"at least {self.draws} draws a frame"


def owned(command_lines, run_dir):
    """The pids of the games started on this run folder's state: never the player's game or
    another run's."""
    marker = (str(Path(run_dir) / "state") + "\\").lower()
    return [pid for pid, command_line in command_lines
            if command_line and marker in command_line.lower()]


def _game_processes():
    script = ("Get-CimInstance Win32_Process -Filter \"Name='fm4_static_recomp.exe'\" | "
              "Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress")
    output = subprocess.run(["pwsh", "-NoProfile", "-Command", script], capture_output=True,
                            text=True, check=True).stdout.strip()
    if not output:
        return []
    found = json.loads(output)
    if isinstance(found, dict):
        found = [found]
    return [(entry["ProcessId"], entry["CommandLine"]) for entry in found]


def _quote(value):
    return "'" + str(value).replace("'", "''") + "'"


class _ExtendedLimits(ctypes.Structure):
    _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD),
                ("SchedulingClass", wintypes.DWORD), ("IoCounters", ctypes.c_uint64 * 6),
                ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]


_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True) if os.name == "nt" else None
if _kernel32:
    _kernel32.OpenProcess.restype = wintypes.HANDLE
    _kernel32.CreateJobObjectW.restype = wintypes.HANDLE
    _kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    _kernel32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
    _kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    _kernel32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    _kernel32.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
                                                  wintypes.DWORD]
    _kernel32.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4

_PROCESS_ACCESS = 0x0001 | 0x0100 | 0x1000 | 0x00100000  # terminate, set quota, query, synchronize
_WAIT_TIMEOUT = 0x102
_KILL_ON_JOB_CLOSE = 0x2000


def _terminate(pid):
    handle = _kernel32.OpenProcess(_PROCESS_ACCESS, False, pid)
    if handle:
        _kernel32.TerminateProcess(handle, 1)
        _kernel32.WaitForSingleObject(handle, 10000)
        _kernel32.CloseHandle(handle)


class WindowsHost:
    """The real game: started through tools\\run.ps1, held in a job that ends it when this
    process ends, so a killed runner leaves no game behind."""

    def __init__(self, input_path, env=None):
        self.input_path = Path(input_path)
        self.extra_env = dict(env or {})
        self.pid = None
        self._handle = None
        self._job = None
        self._start_epoch = 0.0

    def start(self, run_dir, game_args):
        for pid in owned(_game_processes(), run_dir):
            _terminate(pid)
        state = Path(run_dir) / "state"
        self.input_path.write_text("# render test\n", encoding="ascii")
        env = dict(os.environ, **self.extra_env,
                   FM4_STATIC_RECOMP_INPUT_SCRIPT=str(self.input_path),
                   SDL_WINDOW_ACTIVATE_WHEN_SHOWN="0")
        command = (f"& {_quote(TOOLS / 'run.ps1')} -StateRoot {_quote(state)} "
                   f"-GameRoot {_quote(seed.GAME_ROOT)} -GameArguments @("
                   + ",".join(_quote(argument) for argument in game_args) + ")")
        subprocess.run(["pwsh", "-NoProfile", "-Command", command], env=env, check=True,
                       capture_output=True, text=True)
        pids = owned(_game_processes(), run_dir)
        if len(pids) != 1:
            raise RunBroken("launch", f"expected one game on {state}, found {len(pids)}")
        self.pid = pids[0]
        self._handle = _kernel32.OpenProcess(_PROCESS_ACCESS, False, self.pid)
        if not self._handle:
            raise RunBroken("launch", f"cannot open the game's process {self.pid}")
        self._job = _kernel32.CreateJobObjectW(None, None)
        limits = _ExtendedLimits(LimitFlags=_KILL_ON_JOB_CLOSE)
        if not (_kernel32.SetInformationJobObject(self._job, 9, ctypes.byref(limits),
                                                  ctypes.sizeof(limits))
                and _kernel32.AssignProcessToJobObject(self._job, self._handle)):
            raise RunBroken("launch", f"cannot hold the game in a job ({ctypes.get_last_error()})")
        times = [wintypes.FILETIME() for _ in range(4)]
        _kernel32.GetProcessTimes(self._handle, *(ctypes.byref(entry) for entry in times))
        created = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
        self._start_epoch = created / 1e7 - 11644473600

    def alive(self):
        return bool(self._handle) and _kernel32.WaitForSingleObject(self._handle, 0) == _WAIT_TIMEOUT

    def kill(self):
        if self._handle:
            _kernel32.TerminateProcess(self._handle, 1)
            _kernel32.WaitForSingleObject(self._handle, 10000)
            _kernel32.CloseHandle(self._handle)
            self._handle = None
        if self._job:
            _kernel32.CloseHandle(self._job)
            self._job = None

    def now(self):
        return time.monotonic()

    def game_time(self):
        return time.time() - self._start_epoch

    def sleep(self, seconds):
        time.sleep(seconds)

    def append_input(self, line):
        with open(self.input_path, "a", encoding="ascii") as script:
            script.write(line + "\n")


class Session:
    """One game on <run_dir>\\state. A wait sees what the game logged since the runner's last
    action (a press, a hold, an option set, a capture)."""

    def __init__(self, run_dir, game_args, host=None, env=None):
        self.run_dir = Path(run_dir)
        self.state = self.run_dir / "state"
        self.input_path = self.state / "live.input"
        self.game_args = list(game_args)
        self.host = host or WindowsHost(self.input_path, env)
        self.log = LogTail(self.state / "logs" / "runtime.log")
        self.lines = []
        self._mark = 0

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *exc):
        self.stop()
        return False

    @property
    def pid(self):
        return getattr(self.host, "pid", None)

    def start(self):
        self.state.mkdir(parents=True, exist_ok=True)
        self.host.start(self.run_dir, self.game_args)
        self.log = LogTail(self.state / "logs" / "runtime.log")
        self.lines = []
        self._mark = 0

    def stop(self):
        self.host.kill()

    def alive(self):
        return self.host.alive()

    def poll(self):
        new = self.log.poll()
        self.lines.extend(new)
        return new

    def _act(self, step):
        if not self.host.alive():
            raise RunBroken(step, "the game exited")
        self.poll()
        self._mark = len(self.lines)

    def _write(self, hold, controls):
        start = self.host.game_time() + PRESS_LEAD_SECONDS
        self.host.append_input(f"{start:.2f} {hold:.2f} {controls}")

    def press(self, control, hold=0.1):
        self._act(f"press {control}")
        self._write(hold, control)
        self.host.sleep(hold + PRESS_LEAD_SECONDS)

    def hold(self, controls, seconds):
        """Holds controls (e.g. "RT=1") and returns at once."""
        self._act(f"hold {controls}")
        self._write(seconds, controls)

    def set_option(self, name, value):
        self._act(f"set {name}")
        self._write(0.1, f"SET:{name}={value}")
        self.host.sleep(0.1 + PRESS_LEAD_SECONDS)

    def _until(self, signal, deadline, step, mark):
        while True:
            self.poll()
            if signal.ready(self, self.lines[mark:]):
                return True
            if not self.host.alive():
                raise RunBroken(step, "the game exited")
            if self.host.now() >= deadline:
                return False
            self.host.sleep(POLL_SECONDS)

    def wait(self, signal, timeout, step=None):
        step = step or f"wait for {signal!r}"
        if not self._until(signal, self.host.now() + timeout, step, self._mark):
            raise RunBroken(step, f"no {signal!r} within {timeout:g} s")

    def press_until(self, control, signal, timeout=30, every=1.5, hold=0.1, step=None):
        """Presses again every `every` seconds until the signal appears: menus drop presses
        during their transitions."""
        step = step or f"press {control} until {signal!r}"
        self._act(step)
        mark = self._mark
        deadline = self.host.now() + timeout
        while True:
            self._write(hold, control)
            self.host.sleep(hold + PRESS_LEAD_SECONDS)
            if self._until(signal, min(self.host.now() + every, deadline), step, mark):
                return
            if self.host.now() >= deadline:
                raise RunBroken(step, f"no {signal!r} within {timeout:g} s")

    def capture(self, name, timeout=10):
        """The guest output as an Image, kept as <run>\\captures\\<name>.png."""
        step = f"capture {name}"
        self._act(step)
        captures = self.run_dir / "captures"
        captures.mkdir(parents=True, exist_ok=True)
        bmp = captures / f"{name}.bmp"
        bmp.unlink(missing_ok=True)
        request = Path(str(self.input_path) + ".shot")
        pending = Path(str(request) + ".tmp")
        pending.write_text(str(bmp), encoding="ascii")
        os.replace(pending, request)
        deadline = self.host.now() + timeout
        while not bmp.exists():
            if not self.host.alive():
                raise RunBroken(step, "the game exited")
            if self.host.now() >= deadline:
                raise RunBroken(step, f"no capture written within {timeout:g} s")
            self.host.sleep(0.05)
        image = images.read_bmp(bmp)
        images.write_png(captures / f"{name}.png", image)
        bmp.unlink()
        return image
