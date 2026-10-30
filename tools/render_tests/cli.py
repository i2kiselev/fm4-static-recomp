"""python -I tools/render-test.py --tier short|long|full: builds the game, plays the scenarios of
a tier on a fresh copy of a seed, then a PASS/FAIL line per check. Exit 0 all passed, 1 a check
failed, 2 the run broke (the build failed, the game exited, a wait timed out)."""
import argparse
import hashlib
import subprocess
import sys
import traceback
from datetime import datetime
from pathlib import Path

from . import report, scenarios, seed
from .checks import hook_census_check, pipeline_list_check
from .game import RunBroken

RENDER_TESTS = seed.LOCAL / "render-tests"
EXECUTABLE = seed.BUILD / "fm4_static_recomp.exe"


def build(game_root):
    """The normal build: a run after an SDK merge must test the merged game, not the last one
    built. The game folder is resolved first: the last build may be another code image's, or
    this one's never generated. Returns None, or the RunBroken of a failed build."""
    result = subprocess.run(["pwsh", "-NoProfile", "-File", str(seed.PROJECT / "tools" / "codegen.ps1"),
                             "-SkipSdkBuild", "-GameRoot", str(game_root)], capture_output=True,
                            text=True, encoding="utf-8", errors="replace")
    if result.returncode:
        print(result.stdout[-3000:] + result.stderr[-2000:])
        return RunBroken("codegen", f"tools/codegen.ps1 exited {result.returncode}")
    result = subprocess.run(["pwsh", "-NoProfile", "-File", str(seed.PROJECT / "tools" / "build.ps1"),
                             "-Parallel", "10"], capture_output=True, text=True, encoding="utf-8",
                            errors="replace")
    if result.returncode:
        print(result.stdout[-3000:] + result.stderr[-2000:])
        return RunBroken("build", f"tools/build.ps1 exited {result.returncode}")
    return None


def executable_info():
    return {"path": str(EXECUTABLE),
            "modified": datetime.fromtimestamp(EXECUTABLE.stat().st_mtime).isoformat(timespec="seconds"),
            "sha256": hashlib.sha256(EXECUTABLE.read_bytes()).hexdigest()}


def main(argv=None):
    parser = argparse.ArgumentParser(prog="render-test.py", description=__doc__)
    parser.add_argument("--tier", choices=sorted(scenarios.TIERS), default="short")
    parser.add_argument("--only", choices=sorted(scenarios.SCENARIOS),
                        help="run this scenario alone")
    parser.add_argument("--record", action="store_true",
                        help="write the references and figures that are missing")
    parser.add_argument("--force", action="store_true",
                        help="with --record: replace existing references too")
    parser.add_argument("--seed", default="2026-10-24", help="the seed under .local/render-tests/seeds")
    parser.add_argument("--keep-run", action="store_true",
                        help="keep the run's state copy after a passing run")
    parser.add_argument("--no-build", action="store_true",
                        help="test the game as last built (the build is skipped)")
    parser.add_argument("--game-root", type=Path,
                        help="the extracted Disc 1 to play (default: ../extracted/fm4); the build "
                             "is made for its code image, and its run checks the same references")
    args = parser.parse_args(argv)
    if args.force and not args.record:
        parser.error("--force needs --record")
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if args.game_root:
        seed.GAME_ROOT = args.game_root.resolve()

    seed_dir = seed.SEEDS / args.seed
    if not (seed_dir / "seed.json").exists():
        parser.error(f"no seed {seed_dir}")
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    name = f"{stamp}-{args.only or args.tier}"
    run_dir = RENDER_TESTS / "runs" / name
    if not args.no_build:
        print("== build", flush=True)
        broken = build(seed.GAME_ROOT)
        if broken:
            return report.write(run_dir, [], broken)
    seed.prepare_run(seed_dir, run_dir)
    run = scenarios.Run(run_dir, seed_dir, RENDER_TESTS / "references", args.record, args.force)
    run.add(pipeline_list_check(seed.sdk_root(), seed.PIPELINE_LIST))
    names = [args.only] if args.only else scenarios.TIERS[args.tier]
    broken = None
    started = datetime.now()
    for scenario in names:
        print(f"== {scenario}", flush=True)
        try:
            scenarios.SCENARIOS[scenario](run)
        except RunBroken as error:
            broken = RunBroken(f"{scenario}: {error.step}", error.reason)
            break
        except Exception as error:  # a bug or an unexpected game state: the run broke
            traceback.print_exc()
            broken = RunBroken(scenario, f"{type(error).__name__}: {error}")
            break
    if not broken:
        run.add(hook_census_check(RENDER_TESTS / "references" / "hook-census.json",
                                  args.only or args.tier, run.hooks, args.record, args.force))
    print(f"== {run_dir} ({(datetime.now() - started).total_seconds() / 60:.1f} min)")
    code = report.write(run_dir, run.results, broken, executable_info())
    if code == report.EXIT_PASS and not args.keep_run:
        seed.remove_run_state(run_dir, seed_dir)
    return code
