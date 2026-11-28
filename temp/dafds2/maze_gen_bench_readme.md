# DAFds vs DAFds2 benchmark — JSON plan

The benchmark is driven by a JSON file. Each case has its own generator, width, height, count, seed and braid settings. Rectangular mazes are first-class cases.

## Build

Update and rebuild maze-gen so its metadata contains the map_file field:

    cd /c/work/maze-gen
    git pull
    cmake --build build -j

The benchmark solver is compiled automatically by the Python runner with g++.

## Quick test

From the turtles repository root:

    git pull
    python temp/dafds2/run_maze_gen_sweep.py --config temp/dafds2/maze_gen_bench_smoke.json --out temp/gen/maze_gen_json_smoke

The smoke plan runs two mazes for each of three cases, including 10×20 and 20×10 rectangular logical grids.

## Full run

    python temp/dafds2/run_maze_gen_sweep.py --config temp/dafds2/maze_gen_bench_config.json

The checked-in plan contains 12 explicit cases: DFS, Prim and Rooms, each with square and rectangular dimensions. Every case uses the braid values and map count specified in the same JSON file. Change that file to add any dimensions, aspect ratios, seeds, algorithms or counts. No code change is needed.

## JSON dimensions

A case looks like this:

    {
      "name": "prim_wide",
      "algorithm": "prim",
      "cell_width": 31,
      "cell_height": 61,
      "count": 20,
      "braids": [0, 15, 35]
    }

cell_width and cell_height are independent logical maze dimensions. maze-gen exports a wall-and-passage ASCII grid with dimensions:

    map_width  = 2 * cell_width  + 1
    map_height = 2 * cell_height + 1

The generator records both logical and exported dimensions in each JSON metadata file, plus map_file, which points to the paired .txt map.

## Input passed to the solver

The benchmark runner passes each generated maze_XXXXXX.json to the C++ solver using --maze-json. The solver reads map_file from that JSON, opens the matching map, checks its dimensions against map_width/map_height, and then runs DAFds and DAFds2 on that exact map.

## Config structure

- defaults: common count, seed, braids, exit side and Rooms parameters.
- solver.stagnation_limit: same limit for both algorithms.
- cases: array of independent jobs with name, algorithm, cell_width and cell_height. Each case may override defaults.
- keep_mazes: retain both JSON descriptions and ASCII maps; true by default.
- output_dir: base output directory. The runner appends a timestamp to avoid overwriting an earlier run.

## Results

The output directory contains:

- runs.csv — one record per maze, including both command counts, timings, verification and map SHA-256.
- summary.csv — totals, averages, medians, savings and W/T/L by case and braid.
- fallback_events.csv — each DAFds2 fallback and the first merge/exit event.
- config.json — the exact plan and resolved executable paths.
- errors.log — empty on successful completion.
- mazes/<case>/braid_<percent>/*.json and *.txt — source maze files when keep_mazes is true.

## Options

    python temp/dafds2/run_maze_gen_sweep.py --help

Options are intentionally limited to paths and compiler selection. The benchmark matrix belongs in JSON, not in command-line lists.

To publish a run:

    git add temp/gen/<run-folder>
    git commit -m "Add JSON-driven maze-gen benchmark results"
    git push origin main
