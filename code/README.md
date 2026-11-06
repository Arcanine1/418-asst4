# MPI wire routing

Build with the supplied Makefile (MPI C++ compiler, C++20):

```bash
cd code
make
```

Run on PSC in an allocation with enough cores:

```bash
bash run_psc.sh 128 -f inputs/timeinput/medium_wires.txt -b 16 -i 5 -p 0.1
```

The launcher supplies the tested core binding and MPI collective settings.
On a known single-node Slurm allocation, B16 at 128 ranks with more than 4096
wires also uses the tested shared-memory transport settings. The algorithm
chooses reduction groups internally: 8 ranks for up to 512 wires and 16 for
larger inputs. No custom environment variables or experimental build flags
are needed.

For a direct MPI launch, create the output directory first:

```bash
mkdir -p outputs
mpirun -np 8 ./wireroute -f inputs/timeinput/few_wires.txt -b 16 -i 5 -p 0.1
```

The program prints initialization/computation times, validates the final
routing, and reports maximum occupancy and total cost. Output files are
`outputs/wire_output.txt` and `outputs/occ_output.txt`. The internal 8-bit
occupancy representation supports cell counts from 0 through 255.

From the repository root, `make handin.tar` packages the C++ sources/header,
build Makefile, launcher, and this README for submission.
