# DAFds vs DAFds2 benchmark — JSON plan

The benchmark matrix lives in one JSON file. Case width and height are the exact dimensions of the generated ASCII map. Rectangular and even dimensions are supported.

## Build

Update and rebuild maze-gen:

    cd /c/work/maze-gen
    git pull
    cmake --build build -j

The benchmark solver is compiled automatically by the Python runner with g++.

## Quick test

From the turtles repository root:

    git pull
    python temp/dafds2/run_maze_gen_sweep.py --config temp/dafds2/maze_gen_bench_smoke.json --out temp/gen/maze_gen_json_smoke

The smoke plan creates two mazes for each case: 35×63, 63×35 and 35×35.

## Full run

    python temp/dafds2/run_maze_gen_sweep.py --config temp/dafds2/maze_gen_bench_config.json

The checked-in plan contains DFS, Prim and Rooms cases, including square and rectangular maps. Each case specifies its own exact width and height. Edit the JSON to add any dimensions, aspect ratios, seeds, algorithms or counts. No code change is needed.

## JSON dimensions

A case looks like this:

    {
      "name": "prim_wide",
      "algorithm": "prim",
      "width": 83,
      "height": 43,
      "count": 20,
      "braids": [0, 15, 35]
    }

This creates ASCII map files with exactly 83 columns and 43 rows. The generator's internal cell/wall representation is not part of the public interface.

## JSON files passed to the solver

Every generated map has a metadata JSON next to it. The JSON contains map_file, width, height, exit/start coordinates and generation parameters. The runner passes that JSON file to the C++ solver using --maze-json.

The solver reads the paired map filename from JSON, checks the file dimensions against the metadata, and runs DAFds and DAFds2 on that exact map.

## Config structure

- defaults: common count, seed, braids, exit side and Rooms parameters.
- solver.stagnation_limit: same limit for both algorithms.
- cases: array of independent jobs with name, algorithm, width and height. Width/height are exact output dimensions. Each case may override defaults.
- keep_mazes: retain both JSON descriptions and ASCII maps; true by default.
- output_dir: base output directory. The runner appends a timestamp to avoid overwriting an earlier run.

## Results

The output directory contains:

- runs.csv — one record per maze, including exact map dimensions, command counts, timings, verification and map SHA-256.
- summary.csv — totals, averages, medians, savings and W/T/L by case and braid.
- fallback_events.csv — each DAFds2 fallback and the first merge/exit event.
- config.json — the exact plan and resolved executable paths.
- errors.log — empty on successful completion.
- mazes/<case>/braid_<percent>/*.json and *.txt — source maze files when keep_mazes is true.

## Options

    python temp/dafds2/run_maze_gen_sweep.py --help

Only paths and compiler selection are command-line options. The benchmark matrix belongs in JSON, not in command-line dimension lists.

To publish a run:

    git add temp/gen/<run-folder>
    git commit -m "Add JSON-driven maze-gen benchmark results"
    git push origin main
