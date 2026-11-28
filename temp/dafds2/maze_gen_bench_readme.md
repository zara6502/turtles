# DAFds vs DAFds2 benchmark with maze-gen

This benchmark generates maps through the sibling maze-gen repository and runs both solvers on every exact same map.

## Expected workspace layout

    workspace/
      maze-gen/
        build/maze-gen.exe
      turtles/
        temp/dafds2/
          maze_gen_bench.cpp
          run_maze_gen_sweep.py

Build maze-gen once from its repository:

    cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j

## Run

From the turtles repository root:

    python temp/dafds2/run_maze_gen_sweep.py

The benchmark compiles maze_gen_bench.cpp automatically with g++.

Default test matrix:

- Generators: DFS, Prim, Rooms
- Logical sizes: 21, 31, 51, 101
- Braiding: 0, 10, 20, 35 percent
- 20 maps per combination
- DAFds and DAFds2 both use stagnation-limit 1

That is 960 maps total. Both algorithms are run against the same generated map. The S marker is treated as an ordinary free cell: all traversable cells except E are initial turtle positions.

## Quick smoke run

    python temp/dafds2/run_maze_gen_sweep.py --sizes 21 --count 2 --braids 0 --limit 1 --out temp/dafds2/maze_gen_smoke

This runs six maps: two each from DFS, Prim and Rooms.

## Useful options

    python temp/dafds2/run_maze_gen_sweep.py --help

Examples:

- --sizes 21,31,51,101
- --count 20
- --braids 0,10,20,35
- --algorithms dfs,prim,rooms
- --limit 1
- --room-density 40 --room-min 3 --room-max 8
- --seed 20261128
- --keep-mazes to retain generated .txt and .json files
- --generator PATH when maze-gen is not in the expected sibling directory
- --solver PATH to use an already compiled benchmark executable

## Results

Each run creates a timestamped output folder under temp/dafds2/ containing:

- runs.csv — one record per map, including command counts, timings, verification and map SHA-256
- summary.csv — averages, medians, total command savings and W/T/L by generator, size and braid percentage
- fallback_events.csv — DAFds2 fallback episode number, step of first merge/exit event and event type
- config.json — complete test parameters
- errors.log — errors, empty on successful completion

By default, temporary generated maps are deleted after they have been solved. The seed and map hash are stored in runs.csv; the maps can be reproduced with the saved configuration.

To publish results for analysis:

    git add temp/dafds2/maze_gen_sweep_*
    git commit -m "Add maze-gen DAFds benchmark results"
    git push origin main
