# Starter Code Structure

```
code/
├── wireroute.cpp      # Main wire routing program (entry point & algorithm)
├── wireroute.h        # Header: Wire/validate_wire_t structs, wr_checker, option helpers
├── validate.cpp       # Wire and occupancy validation (wr_checker implementation)
├── plot_wires.py      # Python script to visualize wire routing output
├── Makefile           # Build configuration
├── inputs/            # Input test files
│   ├── debug/         # Small boards for debugging and correctness testing
│   ├── problemsize/   # Boards for evaluating scalability
│   │   ├── gridsize/  # Varying grid dimensions
│   │   └── numwires/  # Varying number of wires
│   └── timeinput/     # Boards for benchmarking runtime performance
└── outputs/           # Directory for output files (wire routes & occupancy)
```

### Key source files

- **`wireroute.cpp`** — Contains `main()` with command-line parsing, file I/O, timing, and output writing. The wire routing algorithm itself is left as a **TODO** for students to implement using MPI. 
- **`wireroute.h`** — Defines the `Wire` struct (students may redefine this), `validate_wire_t` (keypoint representation for up to 3 bends), and `wr_checker` for validating consistency between wires and the occupancy grid.
- **`validate.cpp`** — Implements `wr_checker::validate()`, which recomputes occupancy from wire keypoints and checks it against the maintained occupancy grid.
- **`plot_wires.py`** — Reads a wire output file and generates a PNG visualization of the routed wires on the grid.

### Validation / Wire Checker

A built-in **`wr_checker`** is provided to validate that your wire layout is consistent with your occupancy grid. After the computation finishes, the starter code in `main()` already calls it:

```cpp
wr_checker checker(wires, occupancy);
checker.validate();
```

The checker converts each `Wire` to a `validate_wire_t` (a keypoint representation) via `Wire::to_validate_format()`, recomputes the expected occupancy from those keypoints, and compares it against your maintained occupancy grid. If mismatches are found, they are reported; otherwise it prints "Validate Passed."

**Student requirement:** You must implement `Wire::to_validate_format()` at the bottom of `wireroute.cpp`. This method should convert your `Wire` into a `validate_wire_t` by filling in the keypoints array (`p[]`) and setting `num_pts`. The `validate_wire_t` format requires:

- `num_pts` between 2 and `MAX_PTS_PER_WIRE` (5), representing the start, bends, and end of the wire.
- Consecutive keypoints must share the same x or the same y coordinate (i.e., segments are axis-aligned).
- No duplicate consecutive points.

See `wireroute.h` for the `validate_wire_t` struct definition and `validate.cpp` for how the checker validates these constraints.

## Build Instructions

Requires `g++` with C++17 and MPI support.

```bash
make          # Build the wireroute executable
make clean    # Remove compiled objects and the executable
```

This produces the `wireroute` binary in the current directory.

## Usage

### Running `wireroute`

```
mpirun -np <numprocs> ./wireroute -f <input_file> -b <batch_size> [-p <SA_prob>] [-i <SA_iters>]
```

**Required flags:**

| Flag | Description |
|------|-------------|
| `-f` | Path to input file |
| `-n` | Number of MPI threads (must be > 0) |
| `-b` | Batch size for across-wire mode (must be > 0) |

**Optional flags:**

| Flag | Default | Description |
|------|---------|-------------|
| `-p` | `0.1`   | Simulated annealing probability (random exploration vs. greedy optimization) |
| `-i` | `5`     | Number of simulated annealing iterations |

**Example:**

```bash
# 4 threads, 10 SA iterations
mpirun -np 4 ./wireroute -f inputs/debug/sample_8_8wires.txt -i 10

# 8 threads, batch size 4
mpirun -np 8 ./wireroute -f inputs/debug/sample_8_8wires.txt -b 4 -i 5 -p 0.1
```

Output files are written to `outputs/`:
- `outputs/wire_output.txt` — Wire routes in keypoint format
- `outputs/occ_output.txt` — Occupancy grid

### Visualizing with `plot_wires.py`

Requires Python 3 with the `Pillow` library (`pip install Pillow`).

```
python3 plot_wires.py --wires_output_file <wire_file> --wires_output_plot <output_image> [--scale <pixels_per_grid_unit>]
```

| Argument | Default | Description |
|----------|---------|-------------|
| `--wires_output_file` | `output/wire_output.txt` | Path to wire output file from `wireroute` |
| `--wires_output_plot` | `output/wire_plot.png` | Path for the output PNG image |
| `--scale` | `4` | Pixels per grid unit (increase for larger images) |

**Example:**

```bash
# Visualize the default output
python3 plot_wires.py --wires_output_file outputs/wire_output.txt --wires_output_plot outputs/wire_plot.png

# Visualize with higher resolution
python3 plot_wires.py --wires_output_file outputs/wire_output.txt --wires_output_plot outputs/wire_plot.png --scale 8
```
