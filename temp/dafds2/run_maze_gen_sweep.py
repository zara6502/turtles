#!/usr/bin/env python3
"""JSON-configured maze-gen benchmark for DAFds and DAFds2."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shlex
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
DEFAULT_CONFIG = SCRIPT_DIR / "maze_gen_bench_config.json"

RUN_FIELDS = [
    "case_name", "generator", "width", "height",
    "braid", "room_density", "maze_index", "maze_seed", "exit_side", "passages",
    "map_sha256", "metadata_file", "map_file", "free_cells",
    "dafds_commands", "dafds_adaptive", "dafds_fallbacks", "dafds_ms", "dafds_verify",
    "dafds2_commands", "dafds2_adaptive", "dafds2_fallbacks", "dafds2_ms", "dafds2_verify",
    "saving_commands", "saving_percent",
]
EVENT_FIELDS = [
    "case_name", "generator", "width", "height", "braid",
    "maze_index", "maze_seed", "episode", "first_event_step", "event_type",
]
SUMMARY_FIELDS = [
    "case_name", "generator", "width", "height", "braid", "mazes",
    "dafds_avg_commands", "dafds2_avg_commands", "dafds_median_commands", "dafds2_median_commands",
    "dafds_total_commands", "dafds2_total_commands", "saving_commands", "saving_percent",
    "dafds2_wins", "ties", "dafds2_losses",
    "dafds_avg_ms", "dafds2_avg_ms", "dafds2_fallbacks_avg",
    "events", "event_steps_avg", "event_steps_median", "events_le_5_percent",
    "merge_events", "exit_events", "merge_exit_events",
    "merge_events_percent", "exit_events_percent", "verify_failures",
]


def locate_executable(explicit: str | None, label: str, candidates: list[Path]) -> Path:
    if explicit:
        path = Path(explicit).expanduser().resolve()
        if not path.is_file():
            raise FileNotFoundError(f"{label} executable not found: {path}")
        return path
    for path in candidates:
        if path.is_file():
            return path.resolve()
    searched = "\n".join(f"  {p}" for p in candidates)
    raise FileNotFoundError(f"Could not locate {label}. Pass its path explicitly. Searched:\n{searched}")


def compile_solver(compiler_text: str, output: Path) -> None:
    if not SOURCE_FILE.is_file():
        raise FileNotFoundError(f"solver source not found: {SOURCE_FILE}")
    compiler = shlex.split(compiler_text)
    if not compiler:
        raise RuntimeError("compiler command is empty")
    output.parent.mkdir(parents=True, exist_ok=True)
    command = compiler + [
        "-std=c++17", "-O3", "-DNDEBUG", "-Wall", "-Wextra",
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
                "commands": int(fields[1]), "adaptive": int(fields[2]),
                "fallbacks": int(fields[3]), "ms": float(fields[4]), "verify": int(fields[5]),
            }
        elif fields[0] == "DAFDS2" and len(fields) == 6:
            dafds2 = {
                "commands": int(fields[1]), "adaptive": int(fields[2]),
                "fallbacks": int(fields[3]), "ms": float(fields[4]), "verify": int(fields[5]),
            }
        elif fields[0] == "EVENT" and len(fields) == 4:
            events.append({
                "episode": int(fields[1]), "first_event_step": int(fields[2]), "event_type": fields[3],
            })

    if meta is None or dafds is None or dafds2 is None:
        raise RuntimeError("solver output did not contain META, DAFDS and DAFDS2 records:\n" + stdout)
    return meta, dafds, dafds2, events


def load_config(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        config = json.load(f)
    if not isinstance(config, dict) or config.get("format_version") != 1:
        raise ValueError("config must be a JSON object with format_version: 1")

    defaults = config.get("defaults", {})
    solver = config.get("solver", {})
    cases = config.get("cases")
    if not isinstance(defaults, dict) or not isinstance(solver, dict) or not isinstance(cases, list) or not cases:
        raise ValueError("config requires defaults object, solver object and non-empty cases array")

    if int(solver.get("stagnation_limit", 1)) < 1:
        raise ValueError("solver.stagnation_limit must be >= 1")

    names: set[str] = set()
    for i, case in enumerate(cases):
        if not isinstance(case, dict):
            raise ValueError(f"cases[{i}] must be an object")
        name = str(case.get("name", f"case_{i + 1}"))
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", name):
            raise ValueError(f"invalid case name {name!r}; use letters, digits, dot, underscore or hyphen")
        if name in names:
            raise ValueError(f"duplicate case name: {name}")
        names.add(name)
        if case.get("algorithm") not in {"dfs", "prim", "rooms"}:
            raise ValueError(f"cases[{i}].algorithm must be dfs, prim or rooms")
        for field in ("width", "height"):
            value = int(case.get(field, 0))
            if value < 5:
                raise ValueError(f"cases[{i}].{field} must be >= 5")
        count = int(case.get("count", defaults.get("count", 1)))
        if count < 1:
            raise ValueError(f"cases[{i}].count must be >= 1")
        braids = case.get("braids", defaults.get("braids", [0]))
        if not isinstance(braids, list) or not braids or any(int(v) < 0 or int(v) > 100 for v in braids):
            raise ValueError(f"cases[{i}].braids must be a non-empty array of integers from 0 to 100")
        if len(set(int(v) for v in braids)) != len(braids):
            raise ValueError(f"cases[{i}].braids contains duplicates")

    if int(defaults.get("count", 1)) < 1:
        raise ValueError("defaults.count must be >= 1")
    if not 0 <= int(defaults.get("room_density", 40)) <= 100:
        raise ValueError("defaults.room_density must be in 0..100")
    if int(defaults.get("room_min", 3)) < 2 or int(defaults.get("room_max", 8)) < int(defaults.get("room_min", 3)):
        raise ValueError("room_min/room_max are invalid")
    return config


def resolve_relative_to_root(value: str) -> Path:
    p = Path(value).expanduser()
    if not p.is_absolute():
        p = TURTLES_ROOT / p
    return p.resolve()


def write_summary(run_rows: list[dict[str, Any]], event_rows: list[dict[str, Any]], output: Path) -> None:
    grouped: dict[tuple[str, str, int, int, int], list[dict[str, Any]]] = defaultdict(list)
    for row in run_rows:
        key = (row["case_name"], row["generator"], row["width"], row["height"], row["braid"])
        grouped[key].append(row)

    events_by_group: dict[tuple[str, str, int, int, int], list[dict[str, Any]]] = defaultdict(list)
    for event in event_rows:
        key = (event["case_name"], event["generator"], event["width"],
               event["height"], event["braid"])
        events_by_group[key].append(event)

    result_rows: list[dict[str, Any]] = []
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
        result_rows.append({
            "case_name": key[0], "generator": key[1],
            "width": key[2], "height": key[3], "braid": key[4],
            "mazes": len(items),
            "dafds_avg_commands": round(statistics.mean(d1), 3),
            "dafds2_avg_commands": round(statistics.mean(d2), 3),
            "dafds_median_commands": round(statistics.median(d1), 3),
            "dafds2_median_commands": round(statistics.median(d2), 3),
            "dafds_total_commands": total1, "dafds2_total_commands": total2,
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
            "merge_events": merge_count, "exit_events": exit_count, "merge_exit_events": both_count,
            "merge_events_percent": round(merge_count * 100.0 / event_count, 3) if event_count else 0.0,
            "exit_events_percent": round(exit_count * 100.0 / event_count, 3) if event_count else 0.0,
            "verify_failures": sum(1 for x in items if x["dafds_verify"] != 1 or x["dafds2_verify"] != 1),
        })

    with (output / "summary.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=SUMMARY_FIELDS)
        writer.writeheader()
        writer.writerows(result_rows)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run DAFds and DAFds2 using a JSON benchmark plan")
    parser.add_argument("--config", default=str(DEFAULT_CONFIG), help="benchmark plan JSON")
    parser.add_argument("--generator", help="path to maze-gen executable")
    parser.add_argument("--solver", help="use an existing benchmark solver executable")
    parser.add_argument("--compiler", default=os.environ.get("CXX", "g++"), help="C++ compiler (default: g++)")
    parser.add_argument("--out", help="override output directory from config")
    args = parser.parse_args()

    try:
        config_path = Path(args.config).expanduser().resolve()
        config = load_config(config_path)
        defaults = config.get("defaults", {})
        solver_settings = config.get("solver", {})

        generator = locate_executable(args.generator, "maze-gen", [
            WORKSPACE_ROOT / "maze-gen" / "build" / "maze-gen.exe",
            WORKSPACE_ROOT / "maze-gen" / "build" / "maze-gen",
            WORKSPACE_ROOT / "maze-gen" / "build" / "Release" / "maze-gen.exe",
        ])
        if args.solver:
            solver = locate_executable(args.solver, "benchmark solver", [])
        else:
            suffix = ".exe" if os.name == "nt" else ""
            solver = SCRIPT_DIR / "build" / f"maze_gen_bench{suffix}"
            compile_solver(args.compiler, solver)

        output_base = resolve_relative_to_root(args.out or config.get("output_dir", "temp/gen/maze_gen_json_sweep"))
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        output = output_base if args.out else output_base.with_name(output_base.name + "_" + stamp)
        if output.exists() and any(output.iterdir()):
            raise FileExistsError(f"output directory already exists and is not empty: {output}")
        output.mkdir(parents=True, exist_ok=True)

        keep_mazes = bool(config.get("keep_mazes", True))
        limit = int(solver_settings.get("stagnation_limit", 1))
        source_config = json.loads(json.dumps(config))
        source_config["_resolved"] = {
            "config_file": str(config_path),
            "generator": str(generator),
            "solver": str(solver),
            "solver_source": str(SOURCE_FILE),
            "stagnation_limit": limit,
            "keep_mazes": keep_mazes,
        }
        (output / "config.json").write_text(json.dumps(source_config, indent=2) + "\n", encoding="utf-8")
        (output / "errors.log").write_text("", encoding="utf-8")

        plan: list[dict[str, Any]] = []
        for i, case in enumerate(config["cases"]):
            name = str(case.get("name", f"case_{i + 1}"))
            algorithm = str(case["algorithm"])
            width = int(case["width"])
            height = int(case["height"])
            count = int(case.get("count", defaults.get("count", 1)))
            seed = int(case.get("seed", defaults.get("seed", 1)))
            braids = [int(v) for v in case.get("braids", defaults.get("braids", [0]))]
            exit_side = str(case.get("exit_side", defaults.get("exit_side", "any")))
            room_density = int(case.get("room_density", defaults.get("room_density", 40)))
            room_min = int(case.get("room_min", defaults.get("room_min", 3)))
            room_max = int(case.get("room_max", defaults.get("room_max", 8)))
            if seed < 0 or seed > 0xFFFFFFFFFFFFFFFF:
                raise ValueError(f"{name}: seed must be an unsigned 64-bit integer")
            if exit_side not in {"any", "top", "right", "bottom", "left"}:
                raise ValueError(f"{name}: invalid exit_side {exit_side!r}")
            for braid in braids:
                plan.append({
                    "case_name": name, "algorithm": algorithm,
                    "width": width, "height": height,
                    "count": count, "seed": seed, "braid": braid,
                    "exit_side": exit_side, "room_density": room_density,
                    "room_min": room_min, "room_max": room_max,
                })

        total = sum(item["count"] for item in plan)
        completed = 0
        run_rows: list[dict[str, Any]] = []
        event_rows: list[dict[str, Any]] = []
        runs_path = output / "runs.csv"
        events_path = output / "fallback_events.csv"

        with runs_path.open("w", newline="", encoding="utf-8") as runs_file, \
             events_path.open("w", newline="", encoding="utf-8") as events_file:
            run_writer = csv.DictWriter(runs_file, fieldnames=RUN_FIELDS)
            event_writer = csv.DictWriter(events_file, fieldnames=EVENT_FIELDS)
            run_writer.writeheader()
            event_writer.writeheader()

            for job in plan:
                folder = (
                    f"{job['case_name']}/braid_{job['braid']:03d}"
                )
                maze_dir = output / "mazes" / folder
                maze_dir.mkdir(parents=True, exist_ok=True)

                gen_command = [
                    str(generator), "--algorithm", job["algorithm"],
                    "--width", str(job["width"]), "--height", str(job["height"]),
                    "--count", str(job["count"]), "--seed", str(job["seed"]),
                    "--braid", str(job["braid"]), "--exit-side", job["exit_side"],
                    "--format", "ascii", "--out", str(maze_dir),
                ]
                if job["algorithm"] == "rooms":
                    gen_command += [
                        "--room-density", str(job["room_density"]),
                        "--room-min", str(job["room_min"]), "--room-max", str(job["room_max"]),
                    ]

                generated = subprocess.run(gen_command, text=True, capture_output=True)
                if generated.returncode != 0:
                    raise RuntimeError(
                        f"maze-gen failed for {folder}:\n{generated.stdout}\n{generated.stderr}"
                    )

                for index in range(1, job["count"] + 1):
                    meta_path = maze_dir / f"maze_{index:06d}.json"
                    if not meta_path.is_file():
                        raise FileNotFoundError(f"maze-gen did not create metadata JSON: {meta_path}")
                    metadata = json.loads(meta_path.read_text(encoding="utf-8"))
                    map_filename = metadata.get("map_file")
                    if not isinstance(map_filename, str) or not map_filename:
                        raise RuntimeError(
                            "maze-gen metadata has no map_file. Update and rebuild maze-gen from its latest main."
                        )
                    map_path = meta_path.parent / map_filename
                    if not map_path.is_file():
                        raise FileNotFoundError(f"map referenced by metadata JSON does not exist: {map_path}")
                    map_hash = hashlib.sha256(map_path.read_bytes()).hexdigest()

                    solved = subprocess.run(
                        [str(solver), "--maze-json", str(meta_path), "--limit", str(limit)],
                        text=True, capture_output=True,
                    )
                    if solved.returncode != 0:
                        raise RuntimeError(
                            f"solver failed for metadata {meta_path}:\n{solved.stdout}\n{solved.stderr}"
                        )
                    meta, d1, d2, events = parse_solver_output(solved.stdout)
                    saving = d1["commands"] - d2["commands"]
                    rel_meta = str(meta_path.relative_to(output)).replace("\\", "/")
                    rel_map = str(map_path.relative_to(output)).replace("\\", "/")
                    row = {
                        "case_name": job["case_name"], "generator": job["algorithm"],
                        "width": metadata["width"], "height": metadata["height"],
                        "braid": job["braid"],
                        "room_density": job["room_density"] if job["algorithm"] == "rooms" else 0,
                        "maze_index": index, "maze_seed": metadata["seed"],
                        "exit_side": metadata["exit"]["side"], "passages": metadata.get("passages", ""),
                        "map_sha256": map_hash,
                        "metadata_file": rel_meta if keep_mazes else "",
                        "map_file": rel_map if keep_mazes else "",
                        "free_cells": meta["free_cells"],
                        "dafds_commands": d1["commands"], "dafds_adaptive": d1["adaptive"],
                        "dafds_fallbacks": d1["fallbacks"], "dafds_ms": d1["ms"],
                        "dafds_verify": d1["verify"],
                        "dafds2_commands": d2["commands"], "dafds2_adaptive": d2["adaptive"],
                        "dafds2_fallbacks": d2["fallbacks"], "dafds2_ms": d2["ms"],
                        "dafds2_verify": d2["verify"],
                        "saving_commands": saving,
                        "saving_percent": round(saving * 100.0 / d1["commands"], 3) if d1["commands"] else 0.0,
                    }
                    run_rows.append(row)
                    run_writer.writerow(row)
                    runs_file.flush()

                    for event in events:
                        event_row = {
                            "case_name": job["case_name"], "generator": job["algorithm"],
                            "width": job["width"], "height": job["height"],
                            "braid": job["braid"], "maze_index": index,
                            "maze_seed": metadata["seed"], **event,
                        }
                        event_rows.append(event_row)
                        event_writer.writerow(event_row)
                    events_file.flush()

                    completed += 1
                    status = "better" if saving > 0 else ("tie" if saving == 0 else "worse")
                    print(
                        f"[{completed}/{total}] {job['case_name']} "
                        f"{job['width']}x{job['height']} map "
                        f"braid={job['braid']} maze={index}/{job['count']}: "
                        f"{d1['commands']} -> {d2['commands']} ({status})",
                        flush=True,
                    )

                if not keep_mazes:
                    import shutil
                    shutil.rmtree(maze_dir, ignore_errors=True)

        write_summary(run_rows, event_rows, output)
        print(f"\nCompleted: {completed}/{total}")
        print(f"Results: {output}")
        print("Files: runs.csv, summary.csv, fallback_events.csv, config.json, errors.log")
        if keep_mazes:
            print("Generated maze pairs: mazes/<case>/braid_<percent>/*.json + *.txt")
        return 0

    except Exception as exc:
        output_hint = locals().get("output")
        if isinstance(output_hint, Path) and output_hint.exists():
            with (output_hint / "errors.log").open("a", encoding="utf-8") as error_file:
                error_file.write(str(exc) + "\n")
            print(f"Partial results: {output_hint}", file=sys.stderr)
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
