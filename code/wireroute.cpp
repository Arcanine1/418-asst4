/**
 * Parallel VLSI Wire Routing via MPI
 * ayushgar
 */

#include "wireroute.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include <mpi.h>
#include <unistd.h>

const int MAX_UPDATES_PER_MESSAGE = 16;
const int MAX_MESSAGE_JUMPS = 16;
const unsigned int tasks_per_processor = 4;
const float within_wires_threshold = .1;
const bool debug = true;

struct WireTask {
    unsigned int wire_id;
    unsigned int start;
    unsigned int end;
    bool random;
};

struct ProcessTask {
    unsigned int start_task_batch;
    unsigned int end_task_batch;
    unsigned int batch_size;
    unsigned int expected_messages_received_per_iteration;
    double time_taken_to_partition;
};

struct WireUpdate {
    int wire_id;
    int route_index;
    unsigned int cost;
    bool partial;
};

// Best partial candidate seen for each wire in this iteration.
// UINT_MAX means that no improving candidate has been proposed.
void record_partial(const WireUpdate& u, std::vector<WireUpdate>& best_partial) {
    WireUpdate& best = best_partial[u.wire_id];
    if (u.cost < best.cost || (u.cost == best.cost && u.route_index < best.route_index))
        best = u;
}

struct Message {
    unsigned int origin;
    unsigned int num_updates;
    WireUpdate updates[MAX_UPDATES_PER_MESSAGE];
};

struct PendingSend {
    Message msg;
    std::vector<MPI_Request> requests;
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

constexpr unsigned int TREE_FANOUT = 4;

void send_data(const unsigned int pid, const unsigned int nproc, const Message& to_send,
               std::deque<PendingSend>& pending) {

    if (nproc <= 1)
        return;

    // Position of this rank in the logical tree rooted at the origin.
    const unsigned int relative = (pid + nproc - to_send.origin) % nproc;

    // Children in the 4-ary tree.
    const unsigned int first_child = TREE_FANOUT * relative + 1;

    if (first_child >= nproc)
        return;

    const unsigned int num_children = std::min(TREE_FANOUT, nproc - first_child);

    const int bytes = static_cast<int>(offsetof(Message, updates) + to_send.num_updates * sizeof(WireUpdate));

    // Store one stable copy of the message for all child sends.
    pending.emplace_back();
    PendingSend& entry = pending.back();

    entry.msg = to_send;
    entry.requests.resize(num_children);

    for (unsigned int i = 0; i < num_children; ++i) {
        const unsigned int child = first_child + i;
        const unsigned int destination = (to_send.origin + child) % nproc;

        MPI_Isend(&entry.msg, bytes, MPI_BYTE, destination, 0, MPI_COMM_WORLD, &entry.requests[i]);
    }
}

unsigned int receive_and_process_data(const unsigned int pid, const unsigned int nproc, std::vector<Wire>& wires,
                                      std::vector<std::vector<int>>& occupancy, std::vector<WireUpdate>& best_partial,
                                      std::deque<PendingSend>& pending) {
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
        assert(msg.origin < nproc);
        assert(msg.num_updates <= MAX_UPDATES_PER_MESSAGE);
        // Forward each received batch exactly once along its origin's tree.
        send_data(pid, nproc, msg, pending);
        for (unsigned int i = 0; i < msg.num_updates; ++i) {
            const WireUpdate& u = msg.updates[i];
            if (u.partial) {
                record_partial(u, best_partial);
            } else {
                Wire& wire = wires[u.wire_id];
                add_or_remove_wire_from_occupancy(wire, occupancy, false);
                get_ith_wire(wire, u.route_index);
                add_or_remove_wire_from_occupancy(wire, occupancy, true);
            }
        }
        messages_received++;
    }
    return messages_received;
}

void cleanup_pending_sends(std::deque<PendingSend>& pending) {
    while (!pending.empty()) {
        PendingSend& front = pending.front();

        int flag = 0;

        MPI_Testall(static_cast<int>(front.requests.size()), front.requests.data(), &flag, MPI_STATUSES_IGNORE);

        if (flag) {
            pending.pop_front();
        } else {
            break;
        }
    }
}

double get_current_time() {
    if (debug) {
        return MPI_Wtime();
    } else {
        return 0.0;
    }
}

unsigned long long work_of_wire(const Wire& wire, bool is_random) {
    const unsigned int wire_length_cost =
        std::abs(wire.end_x - wire.start_x) + 4 * std::abs(wire.end_y - wire.start_y) + 1;
    if (is_random) {
        return wire_length_cost;
    } else
        return static_cast<unsigned long long>(number_of_possible_wires(wire)) * wire_length_cost;
}

ProcessTask partition_wire(const unsigned int pid, const unsigned int nproc, const unsigned int iteration,
                           const unsigned int initial_batch_size, std::vector<char>& random_wire,
                           std::vector<Wire>& wires, std::vector<WireTask>& wire_work_units, const unsigned int dim_x) {
    ProcessTask task{};
    double split_time_start = get_current_time();

    wire_work_units.clear();

    // Estimate work per wire
    const unsigned int num_wires = wires.size();
    std::vector<unsigned long long> wire_work(num_wires);

    for (size_t i = 0; i < wires.size(); i++) {
        bool is_random = random_wire[num_wires * iteration + i];
        wire_work[i] = work_of_wire(wires[i], is_random);
    }

    unsigned long long total_work = std::accumulate(wire_work.begin(), wire_work.end(), 0ULL);

    // Split only enough to balance several batches per rank. Excess partial
    // tasks multiply all-rank messages and defer otherwise local route updates.
    unsigned long long target = std::max(
        std::max(1ULL, (unsigned long long)(within_wires_threshold * total_work / num_wires)),
        total_work / (static_cast<unsigned long long>(tasks_per_processor) * nproc * initial_batch_size));

    // Split wires into tasks
    for (unsigned int i = 0; i < num_wires; i++) {
        bool is_random = random_wire[num_wires * iteration + i];

        if (is_random) {
            wire_work_units.push_back({i, 0, 0, true});
            continue;
        }

        unsigned int candidates = number_of_possible_wires(wires[i]);

        unsigned int splits =
            nproc == 1 ? 1 : std::min<unsigned long long>(candidates, wire_work[i] ? 1 + (wire_work[i] - 1) / target : 1);

        for (unsigned int j = 0; j < splits; j++) {
            unsigned int start = (uint64_t)candidates * j / splits;
            unsigned int end = (uint64_t)candidates * (j + 1) / splits;
            wire_work_units.push_back({i, start, end, false});
        }
    }

    // Spatially order tasks by wire midpoint using snake-ordered tiles
    constexpr unsigned int TILE_SIZE = 32;
    const unsigned int tiles_x = (dim_x + TILE_SIZE - 1) / TILE_SIZE;

    std::vector<uint64_t> spatial_key(num_wires);

    for (unsigned int i = 0; i < num_wires; i++) {
        const Wire& w = wires[i];

        unsigned int mid_x = (static_cast<unsigned int>(w.start_x) + static_cast<unsigned int>(w.end_x)) / 2;

        unsigned int mid_y = (static_cast<unsigned int>(w.start_y) + static_cast<unsigned int>(w.end_y)) / 2;

        unsigned int tile_x = mid_x / TILE_SIZE;
        unsigned int tile_y = mid_y / TILE_SIZE;

        unsigned int ordered_x = (tile_y % 2 == 0) ? tile_x : (tiles_x - 1 - tile_x);

        spatial_key[i] = static_cast<uint64_t>(tile_y) * tiles_x + ordered_x;
    }

    std::sort(wire_work_units.begin(), wire_work_units.end(), [&](const WireTask& a, const WireTask& b) {
        if (spatial_key[a.wire_id] != spatial_key[b.wire_id])
            return spatial_key[a.wire_id] < spatial_key[b.wire_id];

        if (a.wire_id != b.wire_id)
            return a.wire_id < b.wire_id;

        return a.start < b.start;
    });

    // Adjust batch size based on number of tasks
    const unsigned int total_tasks = wire_work_units.size();

    task.batch_size = std::max(1u, std::min(initial_batch_size, (total_tasks + tasks_per_processor * nproc - 1) /
                                                                    (tasks_per_processor * nproc)));

    const unsigned int number_of_batches = (total_tasks + task.batch_size - 1) / task.batch_size;

    // Compute cumulative batch work
    std::vector<unsigned long long> prefix(number_of_batches + 1, 0);

    for (unsigned int b = 0; b < number_of_batches; b++) {
        unsigned int end = std::min((b + 1) * task.batch_size, total_tasks);

        for (unsigned int i = b * task.batch_size; i < end; i++) {
            const WireTask& wt = wire_work_units[i];

            unsigned int candidates = number_of_possible_wires(wires[wt.wire_id]);

            prefix[b + 1] +=
                wt.random ? wire_work[wt.wire_id] : wire_work[wt.wire_id] * (wt.end - wt.start) / candidates;
        }

        prefix[b + 1] += prefix[b];
    }

    // Partition batches by estimated work (original lower_bound method)
    auto boundary = [&](unsigned int rank) {
        unsigned long long desired = (prefix.back() / nproc) * rank + (prefix.back() % nproc) * rank / nproc;

        return static_cast<unsigned int>(std::lower_bound(prefix.begin(), prefix.end(), desired) - prefix.begin());
    };

    task.start_task_batch = boundary(pid);
    task.end_task_batch = boundary(pid + 1);

    const unsigned int batches_to_process = task.end_task_batch - task.start_task_batch;

    task.expected_messages_received_per_iteration = number_of_batches - batches_to_process;

    double split_time_end = get_current_time();
    task.time_taken_to_partition = split_time_end - split_time_start;

    if (debug) {
        unsigned long long assigned_work = prefix[task.end_task_batch] - prefix[task.start_task_batch];

        printf("pid: %u, total task: %zu, batches_per_processor = %u, batch_size: %u, work: %llu\n", pid,
               wire_work_units.size(), batches_to_process, task.batch_size, assigned_work);
    }

    return task;
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

    unsigned int dim_x = 0, dim_y = 0, num_wires = 0;
    std::vector<Wire> wires;
    std::vector<WireTask> wire_work_units;
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
    MPI_Bcast(metadata, 3, MPI_UNSIGNED, 0, MPI_COMM_WORLD);
    dim_x = metadata[0];
    dim_y = metadata[1];
    num_wires = metadata[2];
    wires.resize(num_wires);
    occupancy.resize(dim_y, std::vector<int>(dim_x));

    // Share wires. Use one bcast to save on communication overhead
    MPI_Bcast(wires.data(), num_wires * sizeof(Wire), MPI_BYTE, 0, MPI_COMM_WORLD);

    // Rank 0 generates random decisions for everyone so that we have better load balancing
    std::vector<char> random_wires(num_wires * SA_iters);

    if (pid == 0) {
        std::mt19937 rng(std::random_device{}());
        std::bernoulli_distribution dist(SA_prob);

        for (auto& x : random_wires)
            x = dist(rng);
    }

    MPI_Bcast(random_wires.data(), random_wires.size(), MPI_CHAR, 0, MPI_COMM_WORLD);

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

    double split_time = 0.0f;
    double send_time = 0.0f;
    double receive_time = 0.0f;
    double stall_time = 0.0f;
    double route_time = 0.0f;
    double sync_time = 0.0f;
    std::mt19937 rng(std::random_device{}());

    for (int SA_iterations = 0; SA_iterations < SA_iters; ++SA_iterations) {
        wire_work_units.clear();
        ProcessTask process_task =
            partition_wire(pid, nproc, SA_iterations, batch_size, random_wires, wires, wire_work_units, dim_x);
        split_time += process_task.time_taken_to_partition;

        const unsigned int total_number_of_tasks = wire_work_units.size();

        unsigned int messages_received = 0;
        std::vector<WireUpdate> best_partial(num_wires, {-1, INT_MAX, UINT_MAX, true});

        for (unsigned int batch_index = process_task.start_task_batch; batch_index < process_task.end_task_batch;
             batch_index++) {
            const unsigned int start_task = batch_index * process_task.batch_size;
            const unsigned int end_task = std::min(start_task + process_task.batch_size, total_number_of_tasks);
            Message msg{};
            msg.origin = pid;
            msg.num_updates = 0;
            for (unsigned int i = start_task; i < end_task; i++) {
                const double route_start_time = get_current_time();
                const WireTask& task = wire_work_units[i];
                Wire& wire = wires[task.wire_id];
                Wire original_wire = wire;
                const unsigned int number_possible_wires = number_of_possible_wires(wire);
                const bool partial = !task.random && (task.start != 0 || task.end != number_possible_wires);

                add_or_remove_wire_from_occupancy(wire, occupancy, false);
                const unsigned int original_wire_cost = test_wire_addition_to_occupancy(original_wire, occupancy);
                bool original_wire_better = true;
                unsigned int best_wire_index = 0;

                if (task.random) {
                    std::uniform_int_distribution<int> int_dist(0, number_possible_wires - 1);
                    best_wire_index = int_dist(rng);
                    original_wire_better = false;
                } else {
                    Wire candidate = original_wire;
                    unsigned int min_cost = UINT_MAX;
                    unsigned int best_wire_index_inner = task.start;
                    for (unsigned int j = task.start; j < task.end; ++j) {
                        get_ith_wire(candidate, j);
                        const unsigned int cost = test_wire_addition_to_occupancy(candidate, occupancy);
                        if (cost < min_cost) {
                            min_cost = cost;
                            best_wire_index_inner = j;
                        }
                    }

                    if (partial) {
                        // Send the best candidate for this partial task. UINT_MAX
                        // signals that it does not improve the original route.
                        WireUpdate update{static_cast<int>(task.wire_id), static_cast<int>(best_wire_index_inner),
                                          min_cost < original_wire_cost ? min_cost : UINT_MAX, true};
                        msg.updates[msg.num_updates++] = update;
                        record_partial(update, best_partial); // Own messages are not received by this rank.
                    } else if (min_cost < original_wire_cost) {
                        best_wire_index = best_wire_index_inner;
                        original_wire_better = false;
                    }
                }

                // Partial tasks do not commit a route until every rank has all proposals.
                if (partial || original_wire_better) {
                    wire = original_wire;
                } else {
                    get_ith_wire(wire, best_wire_index);
                    msg.updates[msg.num_updates++] = {static_cast<int>(task.wire_id), static_cast<int>(best_wire_index),
                                                      0, false};
                }

                add_or_remove_wire_from_occupancy(wire, occupancy, true);
                const double route_end_time = get_current_time();
                route_time += route_end_time - route_start_time;

                messages_received += receive_and_process_data(pid, nproc, wires, occupancy, best_partial, pending);
                const double receive_end_time = get_current_time();
                receive_time += receive_end_time - route_end_time;
            }

            const double send_start_time = get_current_time();
            send_data(pid, nproc, msg, pending);
            cleanup_pending_sends(pending);
            const double send_end_time = get_current_time();
            send_time += send_end_time - send_start_time;
        }

        const double sync_start_time = get_current_time();
        while (messages_received < process_task.expected_messages_received_per_iteration) {
            messages_received += receive_and_process_data(pid, nproc, wires, occupancy, best_partial, pending);
        }
        // Every rank now has all partial proposals. Resolve them identically,
        // breaking ties by the lowest candidate index.
        for (unsigned int wire_id = 0; wire_id < num_wires; ++wire_id) {
            const WireUpdate& best = best_partial[wire_id];
            if (best.cost == UINT_MAX)
                continue;
            Wire& wire = wires[wire_id];
            add_or_remove_wire_from_occupancy(wire, occupancy, false);
            get_ith_wire(wire, static_cast<unsigned int>(best.route_index));
            add_or_remove_wire_from_occupancy(wire, occupancy, true);
        }
        const double sync_end_time = get_current_time();
        sync_time += sync_end_time - sync_start_time;
        MPI_Barrier(MPI_COMM_WORLD);
        const double stall_end_time = get_current_time();
        stall_time += stall_end_time - sync_end_time;
    }

    // Ensure every nonblocking send completes before freeing its buffer / finalizing MPI.
    for (auto& send : pending) {
        MPI_Waitall(static_cast<int>(send.requests.size()), send.requests.data(), MPI_STATUSES_IGNORE);
    }

    pending.clear();
    if (pid == 0) {
        const double compute_time =
            std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - compute_start)
                .count();
        std::cout << "Computation time (sec): " << std::fixed << std::setprecision(10) << compute_time << '\n';
    }

    if (pid == 0) {
        wr_checker checker(wires, occupancy);
        checker.validate();

        /* Write wires and occupancy matrix to files */
        print_stats(occupancy);
        write_output(wires, num_wires, occupancy, dim_x, dim_y);
    }

    if (debug) {

        // send max and average for the different timings zones
        double max_split_time, max_send_time, max_receive_time, max_stall_time, max_sync_time, max_route_time;
        MPI_Reduce(&split_time, &max_split_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        MPI_Reduce(&send_time, &max_send_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        MPI_Reduce(&receive_time, &max_receive_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        MPI_Reduce(&stall_time, &max_stall_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        MPI_Reduce(&sync_time, &max_sync_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        MPI_Reduce(&route_time, &max_route_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

        double avg_split_time, avg_send_time, avg_receive_time, avg_stall_time, avg_sync_time, avg_route_time;
        MPI_Reduce(&split_time, &avg_split_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&send_time, &avg_send_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&receive_time, &avg_receive_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&stall_time, &avg_stall_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&sync_time, &avg_sync_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
        MPI_Reduce(&route_time, &avg_route_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

        if (pid == 0) {
            printf("Max split time: %f \n", max_split_time);
            printf("Max send time: %f \n", max_send_time);
            printf("Max receive time: %f \n", max_receive_time);
            printf("Max sync time: %f \n", max_sync_time);
            printf("Max stall time: %f \n", max_stall_time);
            printf("Max route time: %f \n", max_route_time);
            printf("\n");
            printf("Average split time: %f \n", avg_split_time / nproc);
            printf("Average send time: %f \n", avg_send_time / nproc);
            printf("Average receive time: %f \n", avg_receive_time / nproc);
            printf("Average sync time: %f \n", avg_sync_time / nproc);
            printf("Average stall time: %f \n", avg_stall_time / nproc);
            printf("Average route time: %f \n", avg_route_time / nproc);
        }
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