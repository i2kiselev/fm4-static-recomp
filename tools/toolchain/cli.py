"""python -I tools\\toolchain.py <command> ...: prints one JSON object as its last stdout line.

Exit codes: 0 done, 2 feed or download error, 3 the Microsoft licence was not accepted, 4 not installed.
"""
import argparse
import json
import sys
import time
from pathlib import Path

from toolchain import fetch, msvc, sdk_downloads, windows_sdk

# Where each command's progress sits in setup's ::fm4:: stages.
EVENT_RANGES = {"windows-sdk": ("tools", 26, 28), "msvc": ("tools", 29, 31), "sdk-downloads": ("build", 66, 69)}


class Events:
    def __init__(self, enabled, command):
        self.enabled = enabled
        self.stage, self.low, self.high = EVENT_RANGES.get(command, ("tools", 0, 100))
        self._last = 0.0

    def emit(self, message, fraction=0.0, force=False):
        if not self.enabled:
            return
        now = time.monotonic()
        if not force and now - self._last < 0.5:
            return
        self._last = now
        percent = self.low + round((self.high - self.low) * min(max(fraction, 0.0), 1.0))
        print("::fm4::" + json.dumps({"stage": self.stage, "percent": percent, "message": message},
                                     ensure_ascii=False), flush=True)

    def download(self, what):
        def report(done, total):
            self.emit(f"Downloading {what} ({done >> 20} of {total >> 20} MB).", done / total if total else 0.0,
                      force=done == total)
        return report


def _parser():
    parser = argparse.ArgumentParser(prog="toolchain.py")
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("msvc", "windows-sdk"):
        command = commands.add_parser(name)
        command.add_argument("--root", required=True, type=Path)
        command.add_argument("--downloads", required=True, type=Path)
        command.add_argument("--accept-license", action="store_true")
        command.add_argument("--json-events", action="store_true")
        if name == "msvc":
            command.add_argument("--channel", required=True)
            command.add_argument("--line", required=True)
        else:
            command.add_argument("--version", required=True)
            command.add_argument("--package", action="append", required=True, metavar="ID=SHA256")
            command.add_argument("--base-url", default=windows_sdk.NUGET)
    download = commands.add_parser("download")
    download.add_argument("--url", required=True)
    download.add_argument("--sha256", required=True)
    download.add_argument("--destination", required=True, type=Path)
    download.add_argument("--what", required=True)
    download.add_argument("--stage", default="tools")
    download.add_argument("--percent", nargs=2, type=int, default=[0, 100], metavar=("LOW", "HIGH"))
    download.add_argument("--json-events", action="store_true")
    prefetch = commands.add_parser("sdk-downloads")
    prefetch.add_argument("--sdk-root", required=True, type=Path)
    prefetch.add_argument("--dlss-dir", required=True, type=Path)
    prefetch.add_argument("--build-dir", required=True, type=Path)
    prefetch.add_argument("--base-url", action="append", help="URL template; default GitHub, then jsDelivr")
    prefetch.add_argument("--json-events", action="store_true")
    find = commands.add_parser("find")
    find.add_argument("--line", required=True)
    find.add_argument("--msvc-root", required=True, type=Path)
    find.add_argument("--sdk-version", required=True)
    find.add_argument("--sdk-root", required=True, type=Path)
    return parser


def msvc_record(installed):
    return {"toolset_version": installed.toolset_version, "include": str(installed.include),
            "lib": str(installed.lib), "redist": str(installed.redist)}


def sdk_record(installed):
    return {"sdk_version": installed.sdk_version, "include": [str(p) for p in installed.include],
            "lib": [str(p) for p in installed.lib], "rc": str(installed.rc), "mt": str(installed.mt)}


def _run(args):
    if args.command == "find":
        found_msvc = msvc.find_installed(args.msvc_root, args.line)
        found_sdk = windows_sdk.find_installed(args.sdk_root, args.sdk_version)
        if not found_msvc or not found_sdk:
            return 4, {"error": "missing"}
        return 0, {"msvc": msvc_record(found_msvc), "windows_sdk": sdk_record(found_sdk)}
    events = Events(args.json_events, args.command)
    if args.command == "download":
        events.stage, (events.low, events.high) = args.stage, args.percent
        try:
            # Hosts such as GitHub cap each connection's speed on some players' lines.
            fetch.download([args.url], args.destination, args.sha256, parts=16, progress=events.download(args.what))
        except Exception as error:
            return 2, {"error": "download", "message": str(error)}
        return 0, {"path": str(args.destination)}
    if args.command == "sdk-downloads":
        try:
            events.emit("Fetching the upscaler runtimes (DLSS, FidelityFX).", force=True)
            sdk_downloads.prefetch(sdk_downloads.read_pins(args.sdk_root), args.dlss_dir, args.build_dir,
                                   base_urls=args.base_url or sdk_downloads.BASE_URLS,
                                   progress=lambda name, fraction: events.emit(
                                       f"Fetching the upscaler runtimes: {name}.", fraction))
        except sdk_downloads.PrefetchError as error:
            result = {"error": "sdk-downloads", "message": str(error)}
            if error.dlss_ready:
                result["dlss_dir"] = str(args.dlss_dir)
            return 2, result
        except Exception as error:
            return 2, {"error": "sdk-downloads", "message": str(error)}
        events.emit("The upscaler runtimes are in place.", 1.0, force=True)
        return 0, {"dlss_dir": str(args.dlss_dir)}
    if args.command == "msvc":
        try:
            channel = msvc.read_channel(args.channel)
            licence = msvc.licence_url(channel)
        except Exception as error:
            return 2, {"error": "msvc", "message": str(error)}
        if not args.accept_license:
            return 3, {"error": "licence", "build_tools": licence}
        try:
            events.emit("Reading Microsoft's Visual Studio channel.", force=True)
            resolved = msvc.resolve(args.channel, args.line, channel)
            installed = msvc.install(resolved, args.downloads, args.root,
                                     progress=events.download("Microsoft's C++ runtime"))
        except Exception as error:
            return 2, {"error": "msvc", "message": str(error)}
        events.emit(f"Microsoft's C++ runtime {installed.toolset_version} is in place.", 1.0, force=True)
        return 0, msvc_record(installed)
    if not args.accept_license:
        return 3, {"error": "licence"}
    try:
        packages = [tuple(p.split("=", 1)) for p in args.package]
        installed = windows_sdk.install(args.version, packages, args.downloads, args.root, base_url=args.base_url,
                                        progress=lambda name, fraction: events.emit(
                                            f"Downloading the Windows SDK: {name}.", fraction))
    except Exception as error:
        return 2, {"error": "windows-sdk", "message": str(error)}
    events.emit(f"The Windows SDK {installed.sdk_version} is in place.", 1.0, force=True)
    return 0, sdk_record(installed)


def main(argv=None):
    sys.stdout.reconfigure(encoding="utf-8")
    code, result = _run(_parser().parse_args(argv))
    print(json.dumps(result, ensure_ascii=False), flush=True)
    return code
