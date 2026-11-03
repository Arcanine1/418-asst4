/**
 * Parallel VLSI Wire Routing via MPI
 * ayushgar
 */

#include "wireroute.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <mpi.h>
#include <unistd.h>

const int MAX_UPDATES_PER_MESSAGE = 16;
const int MAX_MESSAGE_JUMPS = 16;

struct WireUpdate {
    int wire_id;
    int route_index;
};

struct Message {
    unsigned int num_updates;
    WireUpdate updates[MAX_UPDATES_PER_MESSAGE];
};

struct PendingSend {
    Message msg;
    MPI_Request request;
};

void print_stats(const std::vector<std::vector<int>>& occupancy) {
    int max_occupancy = 0;
    long long total_cost = 0;

    for (const auto& row : occupancy) {
        for (const int count : row) {
            max_occupancy = std::max(max_occupancy, count);
            total_cost += count * count;
        }
    }

    std::cout << "Max occupancy: " << max_occupancy << '\n';
    std::cout << "Total cost: " << total_cost << '\n';
}

/* This function write the output into 2 files
(1) It write occupancy grids into a file
(2) It convert wires from Wire to validate_wire_t by to_validate_format
(2) It write wires into another file
*/
void write_output(const std::vector<Wire>& wires, const int num_wires, const std::vector<std::vector<int>>& occupancy,
                  const int dim_x, const int dim_y, std::string wires_output_file_path = "outputs/wire_output.txt",
                  std::string occupancy_output_file_path = "outputs/occ_output.txt") {

    std::ofstream out_occupancy(occupancy_output_file_path, std::fstream::out);
    if (!out_occupancy) {
        std::cerr << "Unable to open file: " << occupancy_output_file_path << '\n';
        exit(EXIT_FAILURE);
    }
    out_occupancy << dim_x << ' ' << dim_y << '\n';

    for (const auto& row : occupancy) {
        for (size_t i = 0; i < row.size(); ++i)
            out_occupancy << row[i] << (i == row.size() - 1 ? "" : " ");
        out_occupancy << '\n';
    }
    out_occupancy.close();

    std::ofstream out_wires(wires_output_file_path, std::fstream::out);
    if (!out_wires) {
        std::cerr << "Unable to open file: " << wires_output_file_path << '\n';
        exit(EXIT_FAILURE);
    }

    out_wires << dim_x << ' ' << dim_y << '\n';
    out_wires << num_wires << '\n';

    for (const auto& wire : wires) {
        // NOTICE: we convert to keypoint representation here, using
        // to_validate_format which need to be defined in the bottom of this file
        validate_wire_t keypoints = wire.to_validate_format();
        for (int i = 0; i < keypoints.num_pts; ++i) {
            out_wires << keypoints.p[i].x << ' ' << keypoints.p[i].y;
            if (i < keypoints.num_pts - 1)
                out_wires << ' ';
        }
        out_wires << '\n';
    }

    out_wires.close();
}

bool wires_in_straight_line(const Wire& wire) { return (wire.start_x == wire.end_x) || (wire.start_y == wire.end_y); }

unsigned int number_of_possible_wires(const Wire& wire) {
    if (wires_in_straight_line(wire))
        return 1;

    // Formula from handout
    const unsigned int delta_x = std::abs(wire.start_x - wire.end_x);
    const unsigned int delta_y = std::abs(wire.start_y - wire.end_y);
    return delta_x + delta_y + 2 * (delta_x - 1) * (delta_y - 1);
}

void get_ith_wire(Wire& wire, const unsigned int index) {
    assert(index < number_of_possible_wires(wire));
    if (wires_in_straight_line(wire))
        return;

    const int delta_x = wire.end_x - wire.start_x;
    const int delta_y = wire.end_y - wire.start_y;
    const unsigned int distance_x = std::abs(delta_x);
    const unsigned int distance_y = std::abs(delta_y);

    // Assumes coordinates and distances fit in int.
    const int step_x = delta_x > 0 ? 1 : -1;
    const int step_y = delta_y > 0 ? 1 : -1;

    if (index < distance_x) {
        // Horizontal -> vertical -> horizontal
        const int offset = static_cast<int>(index) + 1;

        wire.mid_x = wire.start_x + step_x * offset;
        wire.mid_y = wire.start_y;
        wire.move_x_start = true;
        wire.move_x_end = true;

    } else if (index < distance_x + distance_y) {
        // Vertical -> horizontal -> vertical
        const int offset = static_cast<int>(index - distance_x) + 1;

        wire.mid_x = wire.start_x;
        wire.mid_y = wire.start_y + step_y * offset;
        wire.move_x_start = false;
        wire.move_x_end = false;

    } else {
        // Three bends
        const auto bend_index = index - (distance_x + distance_y);
        const auto interior_count = (distance_x - 1) * (distance_y - 1);
        const auto position_index = bend_index % interior_count;
        const int x_offset = static_cast<int>(position_index / (distance_y - 1)) + 1;
        const int y_offset = static_cast<int>(position_index % (distance_y - 1)) + 1;
        const bool vertical_first = bend_index >= interior_count;

        wire.mid_x = wire.start_x + step_x * x_offset;
        wire.mid_y = wire.start_y + step_y * y_offset;
        wire.move_x_start = !vertical_first;
        wire.move_x_end = vertical_first;
    }
}

// visit returns if we should continue visiting other cells otherwise we can just halt the iteration early
template <typename Func> void for_each_wire_cell(const Wire& wire, Func visit) {

    // Includes the segment's start, but excludes its end.
    auto visit_segment = [&](int start_x, int start_y, int end_x, int end_y) {
        // wire in straight line
        const bool horizontal = (start_y == end_y);
        const bool vertical = (start_x == end_x);
        assert(horizontal || vertical);

        if (horizontal) {
            const int step_x = horizontal ? (end_x > start_x ? 1 : -1) : 0;
            while (start_x != end_x) {
                visit(start_x, start_y);
                start_x += step_x;
            }
        } else {
            const int step_y = vertical ? (end_y > start_y ? 1 : -1) : 0;
            while (start_y != end_y) {
                visit(start_x, start_y);
                start_y += step_y;
            }
        }
    };

    if (wires_in_straight_line(wire)) {
        visit_segment(wire.start_x, wire.start_y, wire.end_x, wire.end_y);
    } else {
        const int bend1_x = wire.move_x_start ? wire.mid_x : wire.start_x;
        const int bend1_y = wire.move_x_start ? wire.start_y : wire.mid_y;
        const int bend2_x = wire.move_x_end ? wire.mid_x : wire.end_x;
        const int bend2_y = wire.move_x_end ? wire.end_y : wire.mid_y;
        visit_segment(wire.start_x, wire.start_y, bend1_x, bend1_y);
        visit_segment(bend1_x, bend1_y, wire.mid_x, wire.mid_y);
        visit_segment(wire.mid_x, wire.mid_y, bend2_x, bend2_y);
        visit_segment(bend2_x, bend2_y, wire.end_x, wire.end_y);
    }
    // always need to visit the end point of the wire
    visit(wire.end_x, wire.end_y);
}

void add_or_remove_wire_from_occupancy(const Wire& wire, std::vector<std::vector<int>>& occupancy, const bool add) {
    const int delta = add ? 1 : -1;
    for_each_wire_cell(wire, [&](int x, int y) { occupancy[y][x] += delta; });
}

// Returns the total occupancy of the wire if it were to be added to the occupancy grid but not the exact cost. Instead
// we ensure that the correct ordering is met between wires
unsigned int test_wire_addition_to_occupancy(const Wire& wire, std::vector<std::vector<int>>& occupancy) {
    unsigned int acc = 0;
    for_each_wire_cell(wire, [&](int x, int y) { acc += occupancy[y][x]; });
    return acc;
}

// Handle sending new data, forwarding data, and handling pending sends cleanup.
void send_data(const unsigned int pid, const unsigned int nproc, const Message& to_send,
               std::deque<PendingSend>& pending) {

    for (unsigned int i = 0; i < nproc; i++) {
        if (i != pid) {
            pending.emplace_back();
            auto& entry = pending.back();
            entry.msg = to_send;
            MPI_Isend(&entry.msg, sizeof(Message), MPI_BYTE, i, 0, MPI_COMM_WORLD, &entry.request);
        }
    }
}

unsigned int receive_and_process_data(std::vector<Wire>& wires, std::vector<std::vector<int>>& occupancy) {
    unsigned int messages_received = 0;
    while (true) {
        MPI_Status status;
        int new_message_flag;
        MPI_Iprobe(MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &new_message_flag, &status);
        if (!new_message_flag) {
            break;
        }

        Message msg;
        MPI_Recv(&msg, sizeof(Message), MPI_BYTE, status.MPI_SOURCE, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        for (unsigned int i = 0; i < msg.num_updates; ++i) {
            const unsigned int wire_id = msg.updates[i].wire_id;
            const unsigned int route_index = msg.updates[i].route_index;
            Wire& wire = wires[wire_id];
            add_or_remove_wire_from_occupancy(wire, occupancy, false);
            get_ith_wire(wire, route_index);
            add_or_remove_wire_from_occupancy(wire, occupancy, true);
        }
        messages_received++;
    }
    return messages_received;
}

void cleanup_pending_sends(std::deque<PendingSend>& pending) {
    while (!pending.empty()) {
        PendingSend& front = pending.front();
        int flag;
        MPI_Test(&front.request, &flag, MPI_STATUS_IGNORE);
        if (flag) {
            pending.pop_front();
        } else {
            break;
        }
    }
}

int main(int argc, char* argv[]) {
    const auto init_start = std::chrono::steady_clock::now();
    int pid;
    int nproc;

    // Initialize MPI
    MPI_Init(&argc, &argv);
    // Get process rank
    MPI_Comm_rank(MPI_COMM_WORLD, &pid);
    // Get total number of processes
    MPI_Comm_size(MPI_COMM_WORLD, &nproc);

    std::string input_filename;
    double SA_prob = 0.1;
    int SA_iters = 5;
    int batch_size = 1;

    // Read command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "f:p:i:m:b:")) != -1) {
        switch (opt) {
        case 'f':
            input_filename = optarg;
            break;
        case 'p':
            SA_prob = atof(optarg);
            break;
        case 'i':
            SA_iters = atoi(optarg);
            break;
        case 'b':
            batch_size = atoi(optarg);
            break;
        default:
            if (pid == 0) {
                std::cerr << "Usage: " << argv[0] << " -f input_filename [-p SA_prob] [-i SA_iters] -b batch_size\n";
            }

            MPI_Finalize();
            exit(EXIT_FAILURE);
        }
    }

    // Check if required options are provided
    if (empty(input_filename) || SA_iters <= 0 || batch_size <= 0 || batch_size > MAX_UPDATES_PER_MESSAGE) {
        if (pid == 0) {
            std::cerr << "Usage: " << argv[0] << " -f input_filename [-p SA_prob] [-i SA_iters] -b batch_size\n";
        }

        MPI_Finalize();
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        std::cout << "Number of processes: " << nproc << '\n';
        std::cout << "Simulated annealing probability parameter: " << SA_prob << '\n';
        std::cout << "Simulated annealing iterations: " << SA_iters << '\n';
        std::cout << "Input file: " << input_filename << '\n';
        std::cout << "Batch size: " << batch_size << '\n';
    }

    unsigned int dim_x, dim_y, num_wires;
    std::vector<Wire> wires;
    std::vector<std::vector<int>> occupancy;

    if (pid == 0) {
        std::ifstream fin(input_filename);

        if (!fin) {
            std::cerr << "Unable to open file: " << input_filename << ".\n";
            exit(EXIT_FAILURE);
        }

        /* Read the grid dimension and wire information from file */
        fin >> dim_x >> dim_y >> num_wires;

        wires.resize(num_wires);
        for (auto& wire : wires) {
            fin >> wire.start_x >> wire.start_y >> wire.end_x >> wire.end_y;
            wire.move_x_start = true;
            wire.move_x_end = false;
            wire.mid_x = wire.end_x;
            wire.mid_y = wire.start_y;
        }
    }

    // Shares metadata. Use one bcast to save on communication overhead
    unsigned int metadata[3] = {dim_x, dim_y, num_wires};
    MPI_Bcast(metadata, 3, MPI_INT, 0, MPI_COMM_WORLD);
    dim_x = metadata[0];
    dim_y = metadata[1];
    num_wires = metadata[2];
    wires.resize(num_wires);
    occupancy.resize(dim_y, std::vector<int>(dim_x));

    // Share wires. Use one bcast to save on communication overhead
    MPI_Bcast(wires.data(), num_wires * sizeof(Wire), MPI_BYTE, 0, MPI_COMM_WORLD);

    for (const auto& wire : wires) {
        add_or_remove_wire_from_occupancy(wire, occupancy, true);
    }

    /* Initialize any additional data structures needed in the algorithm */
    if (pid == 0) {
        const double init_time =
            std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - init_start)
                .count();
        std::cout << "Initialization time (sec): " << std::fixed << std::setprecision(10) << init_time << '\n';
    }

    // All messages which are currently pending to be sent. Keep track of them and free the memory when the message
    // fully sends
    std::deque<PendingSend> pending;

    const auto compute_start = std::chrono::steady_clock::now();

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> real_dist(0.0, 1.0);
    const unsigned int number_of_batches = (num_wires + batch_size - 1) / batch_size;
    const unsigned int batches_per_processor = (number_of_batches + nproc - 1) / nproc;
    const unsigned int start_batch = batches_per_processor * pid;
    const unsigned int end_batch = std::min(start_batch + batches_per_processor, number_of_batches);
    const unsigned int batches_to_process = end_batch - start_batch;
    const unsigned int expected_messages_received_per_iteration = number_of_batches - batches_to_process;

    double route_time = 0;
    double send_time = 0;
    double recv_time = 0;
    double cleanup_time = 0;
    double stall_time = 0;

    for (int SA_iterations = 0; SA_iterations < SA_iters; ++SA_iterations) {
        unsigned int messages_received = 0;

        for (unsigned int batch_index = start_batch; batch_index < end_batch; batch_index++) {

            const unsigned int start_wire = batch_index * batch_size;
            const unsigned int end_wire = std::min(start_wire + batch_size, num_wires);
            Message msg{};
            msg.num_updates = 0;
            for (unsigned int i = start_wire; i < end_wire; i++) {
                double t0 = MPI_Wtime();
                Wire& wire = wires[i];
                Wire original_wire = wire;
                const unsigned int number_possible_wires = number_of_possible_wires(wire);
                add_or_remove_wire_from_occupancy(wire, occupancy, false);
                unsigned int original_wire_cost = test_wire_addition_to_occupancy(original_wire, occupancy);
                bool original_wire_better = true;
                unsigned int best_wire_index = 0;

                // Simulated Annealing
                if (real_dist(rng) < SA_prob) {
                    std::uniform_int_distribution<int> int_dist(0, number_possible_wires - 1);
                    const int wire_index = int_dist(rng);
                    best_wire_index = wire_index;
                    original_wire_better = false;
                } else {
                    get_ith_wire(wire, 0);
                    unsigned int min_cost = test_wire_addition_to_occupancy(wire, occupancy);
                    unsigned int best_wire_index_inner = 0;
                    for (unsigned int i = 1; i < number_possible_wires; i++) {
                        get_ith_wire(wire, i);

                        unsigned int cost = test_wire_addition_to_occupancy(wire, occupancy);
                        if (min_cost > cost) {
                            min_cost = cost;
                            best_wire_index_inner = i;
                        }
                    }
                    if (min_cost < original_wire_cost) {
                        best_wire_index = best_wire_index_inner;
                        original_wire_better = false;
                    }
                }

                if (original_wire_better) {
                    wire = original_wire;
                } else {
                    get_ith_wire(wire, best_wire_index);
                    msg.updates[msg.num_updates].wire_id = i;
                    msg.updates[msg.num_updates].route_index = best_wire_index;
                    msg.num_updates++;
                }

                add_or_remove_wire_from_occupancy(wire, occupancy, true);
                double t1 = MPI_Wtime();
                route_time += t1 - t0;
                messages_received += receive_and_process_data(wires, occupancy);
                double t2 = MPI_Wtime();
                recv_time += t2 - t1;
            }

            double t3 = MPI_Wtime();
            send_data(pid, nproc, msg, pending);
            double t4 = MPI_Wtime();

            cleanup_pending_sends(pending);
            double t5 = MPI_Wtime();

            send_time += t4 - t3;
            cleanup_time += t5 - t4;
        }

        double t6 = MPI_Wtime();
        while (messages_received < expected_messages_received_per_iteration) {
            messages_received += receive_and_process_data(wires, occupancy);
        }
        double t7 = MPI_Wtime();
        stall_time += t7 - t6;
    }

    if (pid == 0) {
        const double compute_time =
            std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - compute_start)
                .count();
        std::cout << "Computation time (sec): " << std::fixed << std::setprecision(10) << compute_time << '\n';
    }

    printf("Routing: %f s\n", route_time);
    printf("Send: %f s\n", send_time);
    printf("Recv: %f s\n", recv_time);
    printf("Cleanup: %f s\n", cleanup_time);
    printf("Stall Time: %f s\n", stall_time);

    if (pid == 0) {
        wr_checker checker(wires, occupancy);
        checker.validate();

        /* Write wires and occupancy matrix to files */
        print_stats(occupancy);
        write_output(wires, num_wires, occupancy, dim_x, dim_y);
    }

    // Cleanup
    MPI_Finalize();
}

validate_wire_t Wire::to_validate_format(void) const {
    validate_wire_t w{};
    int number_of_points = 1;
    const int bend1_x = move_x_start ? mid_x : start_x;
    const int bend1_y = move_x_start ? start_y : mid_y;
    const int bend2_x = move_x_end ? mid_x : end_x;
    const int bend2_y = move_x_end ? end_y : mid_y;
    w.p[0].x = start_x;
    w.p[0].y = start_y;

    if (start_x != bend1_x || start_y != bend1_y) {
        w.p[number_of_points].x = bend1_x;
        w.p[number_of_points].y = bend1_y;
        number_of_points++;
    }

    if (bend1_x != mid_x || bend1_y != mid_y) {
        w.p[number_of_points].x = mid_x;
        w.p[number_of_points].y = mid_y;
        number_of_points++;
    }

    if (mid_x != bend2_x || mid_y != bend2_y) {
        w.p[number_of_points].x = bend2_x;
        w.p[number_of_points].y = bend2_y;
        number_of_points++;
    }

    if (bend2_x != end_x || bend2_y != end_y) {
        w.p[number_of_points].x = end_x;
        w.p[number_of_points].y = end_y;
        number_of_points++;
    }

    w.num_pts = number_of_points;
    return w;
}