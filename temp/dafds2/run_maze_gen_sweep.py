#!/usr/bin/env python3
"""Generate reproducible maze batches and compare DAFds with DAFds2."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import shlex
import shutil
import statistics
import subprocess
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
TURTLES_ROOT = SCRIPT_DIR.parents[1]
WORKSPACE_ROOT = TURTLES_ROOT.parent
SOURCE_FILE = SCRIPT_DIR / "maze_gen_bench.cpp"

RUN_FIELDS = [
    "generator", "width", "height", "generator_width", "generator_height",
    "braid", "room_density", "maze_index",
    "maze_seed", "exit_side", "passages", "map_sha256", "maze_file", "free_cells",
    "dafds_commands", "dafds_adaptive", "dafds_fallbacks", "dafds_ms", "dafds_verify",
    "dafds2_commands", "dafds2_adaptive", "dafds2_fallbacks", "dafds2_ms", "dafds2_verify",
    "saving_commands", "saving_percent",
]
EVENT_FIELDS = [
    "generator", "width", "height", "braid", "maze_index", "maze_seed",
    "episode", "first_event_step", "event_type",
]
SUMMARY_FIELDS = [
    "generator", "width", "height", "braid", "mazes",
    "dafds_avg_commands", "dafds2_avg_commands", "dafds_median_commands", "dafds2_median_commands",
    "dafds_total_commands", "dafds2_total_commands", "saving_commands", "saving_percent",
    "dafds2_wins", "ties", "dafds2_losses",
    "dafds_avg_ms", "dafds2_avg_ms", "dafds2_fallbacks_avg",
    "events", "event_steps_avg", "event_steps_median", "events_le_5_percent",
    "merge_events", "exit_events", "merge_exit_events",
    "merge_events_percent", "exit_events_percent", "verify_failures",
]


def parse_int_list(value: str, option: str) -> list[int]:
    try:
        result = [int(part.strip()) for part in value.split(",") if part.strip()]
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"{option}: expected comma-separated integers") from exc
    if not result:
        raise argparse.ArgumentTypeError(f"{option}: list cannot be empty")
    return result


def locate_generator(explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit).expanduser().resolve()
        if not path.is_file():
            raise FileNotFoundError(f"maze-gen executable not found: {path}")
        return path

    candidates = [
        WORKSPACE_ROOT / "maze-gen" / "build" / "maze-gen.exe",
        WORKSPACE_ROOT / "maze-gen" / "build" / "maze-gen",
        WORKSPACE_ROOT / "maze-gen" / "build" / "Release" / "maze-gen.exe",
        Path.cwd() / "maze-gen" / "build" / "maze-gen.exe",
        Path.cwd() / "maze-gen" / "build" / "maze-gen",
    ]
    for path in candidates:
        if path.is_file():
            return path.resolve()
    searched = "\n".join(f"  {p}" for p in candidates)
    raise FileNotFoundError(
        "Could not locate maze-gen. Build it first or pass --generator PATH. Searched:\n" + searched
    )


def compile_solver(compiler_text: str, output: Path) -> None:
    if not SOURCE_FILE.is_file():
        raise FileNotFoundError(f"solver source not found: {SOURCE_FILE}")
    compiler = shlex.split(compiler_text)
    if not compiler:
        raise RuntimeError("compiler command is empty")
    output.parent.mkdir(parents=True, exist_ok=True)
    command = compiler + [
        "-std=c++17", "-O3", "-DNDEBUG", "-Wall", "-Wextra", "-pedantic",
        str(SOURCE_FILE), "-o", str(output),
    ]
    print("Building benchmark solver:", " ".join(command), flush=True)
    subprocess.run(command, check=True)


def parse_solver_output(stdout: str) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any], list[dict[str, Any]]]:
    meta: dict[str, Any] | None = None
    dafds: dict[str, Any] | None = None
    dafds2: dict[str, Any] | None = None
    events: list[dict[str, Any]] = []

    for raw in stdout.splitlines():
        fields = raw.strip().split("\t")
        if not fields:
            continue
        if fields[0] == "META" and len(fields) == 2:
            meta = {"free_cells": int(fields[1])}
        elif fields[0] == "DAFDS" and len(fields) == 6:
            dafds = {
                "commands": int(fields[1]),
                "adaptive": int(fields[2]),
                "fallbacks": int(fields[3]),
                "ms": float(fields[4]),
                "verify": int(fields[5]),
            }
        elif fields[0] == "DAFDS2" and len(fields) == 6:
            dafds2 = {
                "commands": int(fields[1]),
                "adaptive": int(fields[2]),
                "fallbacks": int(fields[3]),
                "ms": float(fields[4]),
                "verify": int(fields[5]),
            }
        elif fields[0] == "EVENT" and len(fields) == 4:
            events.append({
                "episode": int(fields[1]),
                "first_event_step": int(fields[2]),
                "event_type": fields[3],
            })

    if meta is None or dafds is None or dafds2 is None:
        raise RuntimeError("solver output did not contain META, DAFDS and DAFDS2 records:\n" + stdout)
    return meta, dafds, dafds2, events


def write_summary(run_rows: list[dict[str, Any]], event_rows: list[dict[str, Any]], output: Path) -> None:
    grouped: dict[tuple[str, int, int, int], list[dict[str, Any]]] = defaultdict(list)
    for row in run_rows:
        grouped[(row["generator"], row["width"], row["height"], row["braid"])].append(row)

    events_by_group: dict[tuple[str, int, int, int], list[dict[str, Any]]] = defaultdict(list)
    for event in event_rows:
        key = (event["generator"], event["width"], event["height"], event["braid"])
        events_by_group[key].append(event)

    rows: list[dict[str, Any]] = []
    for key, items in sorted(grouped.items()):
        event_items = events_by_group[key]
        d1 = [int(x["dafds_commands"]) for x in items]
        d2 = [int(x["dafds2_commands"]) for x in items]
        total1, total2 = sum(d1), sum(d2)
        wins = sum(1 for a, b in zip(d1, d2) if b < a)
        ties = sum(1 for a, b in zip(d1, d2) if b == a)
        losses = sum(1 for a, b in zip(d1, d2) if b > a)
        steps = [int(x["first_event_step"]) for x in event_items]
        merge_count = sum(1 for x in event_items if x["event_type"] == "merge")
        exit_count = sum(1 for x in event_items if x["event_type"] == "exit")
        both_count = sum(1 for x in event_items if x["event_type"] == "merge+exit")
        event_count = len(event_items)
        rows.append({
            "generator": key[0], "width": key[1], "height": key[2], "braid": key[3],
            "mazes": len(items),
            "dafds_avg_commands": round(statistics.mean(d1), 3),
            "dafds2_avg_commands": round(statistics.mean(d2), 3),
            "dafds_median_commands": round(statistics.median(d1), 3),
            "dafds2_median_commands": round(statistics.median(d2), 3),
            "dafds_total_commands": total1,
            "dafds2_total_commands": total2,
            "saving_commands": total1 - total2,
            "saving_percent": round((total1 - total2) * 100.0 / total1, 3) if total1 else 0.0,
            "dafds2_wins": wins, "ties": ties, "dafds2_losses": losses,
            "dafds_avg_ms": round(statistics.mean(float(x["dafds_ms"]) for x in items), 3),
            "dafds2_avg_ms": round(statistics.mean(float(x["dafds2_ms"]) for x in items), 3),
            "dafds2_fallbacks_avg": round(statistics.mean(int(x["dafds2_fallbacks"]) for x in items), 3),
            "events": event_count,
            "event_steps_avg": round(statistics.mean(steps), 3) if steps else 0,
            "event_steps_median": statistics.median(steps) if steps else 0,
            "events_le_5_percent": round(sum(1 for x in steps if x <= 5) * 100.0 / event_count, 3) if event_count else 0.0,
            "merge_events": merge_count,
            "exit_events": exit_count,
            "merge_exit_events": both_count,
            "merge_events_percent": round(merge_count * 100.0 / event_count, 3) if event_count else 0.0,
            "exit_events_percent": round(exit_count * 100.0 / event_count, 3) if event_count else 0.0,
            "verify_failures": sum(1 for x in items if x["dafds_verify"] != 1 or x["dafds2_verify"] != 1),
        })

    with (output / "summary.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=SUMMARY_FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate mazes and compare DAFds with DAFds2")
    parser.add_argument("--generator", help="path to maze-gen executable; auto-detected beside turtles repo by default")
    parser.add_argument("--solver", help="use an already-built benchmark executable instead of compiling")
    parser.add_argument("--compiler", default=os.environ.get("CXX", "g++"), help="C++ compiler command (default: g++)")
    parser.add_argument("--sizes", default="21,31,51,101", help="comma-separated exported map dimensions (odd numbers, e.g. 21,31,51,101)")
    parser.add_argument("--count", type=int, default=20, help="mazes per generator/size/braid combination")
    parser.add_argument("--braids", default="0,10,20,35", help="comma-separated maze-gen braid percentages")
    parser.add_argument("--algorithms", default="dfs,prim,rooms", help="comma-separated maze-gen algorithms")
    parser.add_argument("--seed", type=int, default=20261128, help="maze-gen base seed")
    parser.add_argument("--limit", type=int, default=1, help="shared DAFds/DAFds2 stagnation limit")
    parser.add_argument("--room-density", type=int, default=40, help="maze-gen room density for rooms algorithm")
    parser.add_argument("--room-min", type=int, default=3, help="maze-gen minimum room side")
    parser.add_argument("--room-max", type=int, default=8, help="maze-gen maximum room side")
    parser.add_argument("--exit-side", choices=["any", "top", "right", "bottom", "left"], default="any")
    parser.add_argument("--out", help="output directory; timestamped directory under this script by default")
    parser.add_argument("--keep-mazes", action="store_true", help="keep generated ASCII maps and metadata JSON")
    args = parser.parse_args()

    try:
        sizes = parse_int_list(args.sizes, "--sizes")
        braids = parse_int_list(args.braids, "--braids")
    except argparse.ArgumentTypeError as exc:
        parser.error(str(exc))
    algorithms = [x.strip().lower() for x in args.algorithms.split(",") if x.strip()]
    if not algorithms or any(x not in {"dfs", "prim", "rooms"} for x in algorithms):
        parser.error("--algorithms must contain dfs, prim and/or rooms")
    if len(set(algorithms)) != len(algorithms):
        parser.error("--algorithms must not contain duplicates")
    if any(x < 5 or x % 2 == 0 for x in sizes):
        parser.error("--sizes must be odd exported-map dimensions >= 5")
    if args.count < 1 or args.limit < 1:
        parser.error("--count and --limit must be >= 1")
    if any(x < 0 or x > 100 for x in braids):
        parser.error("all braid values must be in 0..100")
    if len(set(braids)) != len(braids):
        parser.error("--braids must not contain duplicates")
    if not 0 <= args.room_density <= 100 or args.room_min < 2 or args.room_max < args.room_min:
        parser.error("invalid room settings")
    if args.seed < 0 or args.seed > 0xFFFFFFFFFFFFFFFF:
        parser.error("--seed must be an unsigned 64-bit integer")

    generator = locate_generator(args.generator)
    if args.solver:
        solver = Path(args.solver).expanduser().resolve()
        if not solver.is_file():
            raise FileNotFoundError(f"benchmark executable not found: {solver}")
    else:
        suffix = ".exe" if os.name == "nt" else ""
        solver = SCRIPT_DIR / "build" / f"maze_gen_bench{suffix}"
        compile_solver(args.compiler, solver)

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output = Path(args.out).expanduser().resolve() if args.out else SCRIPT_DIR / f"maze_gen_sweep_{stamp}"
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise FileExistsError(f"output directory already exists and is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    (output / "errors.log").write_text("", encoding="utf-8")

    config = {
        "build_id": "maze-gen-dafds-dafds2-bench-v2-20261128",
        "generator": str(generator),
        "solver": str(solver),
        "solver_source": str(SOURCE_FILE),
        "sizes": sizes,
        "size_semantics": "exported ASCII map dimensions",
        "maze_gen_cell_sizes": [(x - 1) // 2 for x in sizes],
        "count_per_combination": args.count,
        "braids": braids,
        "algorithms": algorithms,
        "base_seed": args.seed,
        "stagnation_limit": args.limit,
        "room_density": args.room_density,
        "room_min": args.room_min,
        "room_max": args.room_max,
        "exit_side": args.exit_side,
        "keep_mazes": args.keep_mazes,
    }
    (output / "config.json").write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")

    total = len(algorithms) * len(sizes) * len(braids) * args.count
    completed = 0
    run_rows: list[dict[str, Any]] = []
    event_rows: list[dict[str, Any]] = []

    runs_path = output / "runs.csv"
    events_path = output / "fallback_events.csv"
    try:
        with runs_path.open("w", newline="", encoding="utf-8") as runs_file, \
             events_path.open("w", newline="", encoding="utf-8") as events_file:
            run_writer = csv.DictWriter(runs_file, fieldnames=RUN_FIELDS)
            event_writer = csv.DictWriter(events_file, fieldnames=EVENT_FIELDS)
            run_writer.writeheader()
            event_writer.writeheader()

            for algorithm in algorithms:
                for size in sizes:
                    generator_cells = (size - 1) // 2
                    for braid in braids:
                        combo_name = f"{size}x{size}_b{braid}"
                        if args.keep_mazes:
                            maze_dir = output / "mazes" / algorithm / combo_name
                            maze_dir.mkdir(parents=True, exist_ok=True)
                        else:
                            maze_dir = output / ".work" / algorithm / combo_name
                            maze_dir.mkdir(parents=True, exist_ok=True)

                        gen_command = [
                            str(generator), "--algorithm", algorithm,
                            "--width", str(generator_cells), "--height", str(generator_cells),
                            "--count", str(args.count), "--seed", str(args.seed),
                            "--braid", str(braid), "--exit-side", args.exit_side,
                            "--format", "ascii", "--out", str(maze_dir),
                        ]
                        if algorithm == "rooms":
                            gen_command += [
                                "--room-density", str(args.room_density),
                                "--room-min", str(args.room_min),
                                "--room-max", str(args.room_max),
                            ]
                        gen = subprocess.run(gen_command, text=True, capture_output=True)
                        if gen.returncode != 0:
                            raise RuntimeError(
                                f"maze-gen failed for {algorithm} {combo_name} braid={braid}:\n"
                                f"{gen.stdout}\n{gen.stderr}"
                            )

                        for index in range(1, args.count + 1):
                            map_path = maze_dir / f"maze_{index:06d}.txt"
                            meta_path = maze_dir / f"maze_{index:06d}.json"
                            if not map_path.is_file() or not meta_path.is_file():
                                raise FileNotFoundError(f"maze-gen did not create expected map/metadata: {map_path}")
                            metadata = json.loads(meta_path.read_text(encoding="utf-8"))
                            map_hash = hashlib.sha256(map_path.read_bytes()).hexdigest()
                            solve = subprocess.run(
                                [str(solver), "--maze", str(map_path), "--limit", str(args.limit)],
                                text=True, capture_output=True,
                            )
                            if solve.returncode != 0:
                                raise RuntimeError(
                                    f"solver failed for {map_path}:\n{solve.stdout}\n{solve.stderr}"
                                )
                            meta, d1, d2, events = parse_solver_output(solve.stdout)
                            saving = d1["commands"] - d2["commands"]
                            row = {
                                "generator": algorithm,
                                "width": size,
                                "height": size,
                                "generator_width": generator_cells,
                                "generator_height": generator_cells,
                                "braid": braid,
                                "room_density": args.room_density if algorithm == "rooms" else 0,
                                "maze_index": index,
                                "maze_seed": metadata["seed"],
                                "exit_side": metadata["exit"]["side"],
                                "passages": metadata.get("passages", ""),
                                "map_sha256": map_hash,
                                "maze_file": str(map_path.relative_to(output)) if args.keep_mazes else "",
                                "free_cells": meta["free_cells"],
                                "dafds_commands": d1["commands"],
                                "dafds_adaptive": d1["adaptive"],
                                "dafds_fallbacks": d1["fallbacks"],
                                "dafds_ms": d1["ms"],
                                "dafds_verify": d1["verify"],
                                "dafds2_commands": d2["commands"],
                                "dafds2_adaptive": d2["adaptive"],
                                "dafds2_fallbacks": d2["fallbacks"],
                                "dafds2_ms": d2["ms"],
                                "dafds2_verify": d2["verify"],
                                "saving_commands": saving,
                                "saving_percent": round(saving * 100.0 / d1["commands"], 3) if d1["commands"] else 0.0,
                            }
                            run_writer.writerow(row)
                            runs_file.flush()
                            run_rows.append(row)

                            for event in events:
                                event_row = {
                                    "generator": algorithm,
                                    "width": size,
                                    "height": size,
                                    "braid": braid,
                                    "maze_index": index,
                                    "maze_seed": metadata["seed"],
                                    **event,
                                }
                                event_writer.writerow(event_row)
                                event_rows.append(event_row)
                            events_file.flush()

                            completed += 1
                            delta = "better" if saving > 0 else ("tie" if saving == 0 else "worse")
                            print(
                                f"[{completed}/{total}] {algorithm} {combo_name} b{braid} "
                                f"maze={index}/{args.count}: {d1['commands']} -> {d2['commands']} ({delta})",
                                flush=True,
                            )

                        if not args.keep_mazes:
                            shutil.rmtree(maze_dir, ignore_errors=True)
                            parent = maze_dir.parent
                            try:
                                parent.rmdir()
                                (output / ".work").rmdir()
                            except OSError:
                                pass

    except Exception as exc:
        with (output / "errors.log").open("a", encoding="utf-8") as error_file:
            error_file.write(str(exc) + "\n")
        print(f"ERROR: {exc}", file=sys.stderr)
        print(f"Partial results: {output}", file=sys.stderr)
        return 1

    write_summary(run_rows, event_rows, output)
    print(f"\nCompleted: {completed}/{total}")
    print(f"Results: {output}")
    print("Files: runs.csv, summary.csv, fallback_events.csv, config.json, errors.log")
    if args.keep_mazes:
        print("Generated maps: mazes/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
