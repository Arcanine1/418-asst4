# MPI wire routing

Build with the supplied Makefile:

```bash
cd code
make
mkdir -p outputs
mpirun -np 128 ./wireroute -f inputs/timeinput/medium_wires.txt -b 16 -i 5 -p 0.1
```

Use the MPI launch command allowed by the assignment. The code does not
require a custom launcher, MCA parameters, transport overrides, or binding
options. Reduction groups are chosen internally: 8 ranks for up to 512 wires
and 16 otherwise. All MPI communication is implemented in the C++ source.

The program prints initialization/computation times, validates the final
routing, and reports maximum occupancy and total cost. Output files are
outputs/wire_output.txt and outputs/occ_output.txt. The internal 8-bit
occupancy representation supports cell counts from 0 through 255.

The root Makefile's submission rule packages only code/Makefile and the C++
headers and sources. Do not regenerate handin.tar until code review is done.
