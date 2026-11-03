#include "wireroute.h"
#include <algorithm>
#include <assert.h>
#include <stdio.h>

#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define RESET "\x1b[0m"

// YOU DON'T NEED TO CHANGE THIS FILE

void validate_wire_t::print_wire() const {
  for (int i = 0; i < num_pts; i++) {
    printf("[%u %u]", p[i].x, p[i].y);
    if (i != num_pts - 1) {
      printf(" -> ");
    } else {
      printf("\n");
    }
  }
}

validate_wire_t &validate_wire_t::cleanup(void) {
  if (num_pts < 2 || num_pts > MAX_PTS_PER_WIRE) {
    printf("cleanup: invalid num_pts=%u (expected 2..%d): ", num_pts,
           MAX_PTS_PER_WIRE);
    print_wire();
    abort();
  }

  // (1) check no duplication
  for (int i = 0; i < num_pts; i++) {
    for (int j = i + 1; j < num_pts; j++) {
      if (p[i].x == p[j].x && p[i].y == p[j].y) {
        printf("cleanup: duplicate point at indices %d and %d: ", i, j);
        print_wire();
        abort();
      }
    }
  }

  // (2) check consecutive key_points share x or y
  for (int i = 0; i < num_pts - 1; i++) {
    if (!(p[i].x == p[i + 1].x || p[i].y == p[i + 1].y)) {
      printf("cleanup: badwire with bad points at idx %d->%d: ", i, i + 1);
      print_wire();
      abort();
    }
  }
  return *this;
}

void wr_checker::validate() const {
  std::vector occ_computed(dim_y, std::vector<int>(dim_x));

  for (int wi = 0; wi < nwires; wi++) {
    const auto &w = wires[wi];
    validate_wire_t wire = w.to_validate_format().cleanup();

    const auto &pts = wire.p;
    // Fill occupancy matrix
    int cur_point = 0;
    do {
      int x = pts[cur_point].x;
      int y = pts[cur_point].y;
      int x_n = pts[cur_point + 1].x;
      int y_n = pts[cur_point + 1].y;
      int x_step = (x_n > x) ? 1 : (x_n < x) ? -1 : 0;
      int y_step = (y_n > y) ? 1 : (y_n < y) ? -1 : 0;
      while (x != x_n || y != y_n) {
        occ_computed[y][x]++;
        x += x_step;
        y += y_step;
      }
      // include last point if on last line segment
      if (++cur_point == wire.num_pts - 1) {
        occ_computed[y][x]++;
      }
      assert(x == x_n && y == y_n);
    } while (cur_point < wire.num_pts - 1);
  }
  int total = 0;
  for (int i = 0; i < dim_y; i++) {
    for (int j = 0; j < dim_x; j++) {
      if (occ_computed[i][j] != occupancies[i][j]) {
        if (total++ < COST_REPORT_DEPTH)
          printf("Occupancy Matrix: Values mismatch at (%d, %d)\n", j, i);
      }
    }
  }
  if (total > 0)
    printf(RED "Validate: %d total mismatches.\n" RESET, total);
  else
    printf(GREEN "Validate Passed: no mismatches.\n" RESET);
}
