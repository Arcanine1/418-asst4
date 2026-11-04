#include "wireroute.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
    unsigned int start_task;
    unsigned int end_task;
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

void print_stats(const std::vector<int>& occupancy, const int dim_x, const int dim_y) {
    int max_occupancy = 0;
    long long total_cost = 0;

    for (const int count : occupancy) {
        max_occupancy = std::max(max_occupancy, count);
        total_cost += static_cast<long long>(count) * count;
    }

    std::cout << "Max occupancy: " << max_occupancy << '\n';
    std::cout << "Total cost: " << total_cost << '\n';
}

void write_output(const std::vector<Wire>& wires, const int num_wires, const std::vector<int>& occupancy,
                  const int dim_x, const int dim_y, std::string wires_output_file_path = "outputs/wire_output.txt",
                  std::string occupancy_output_file_path = "outputs/occ_output.txt") {

    std::ofstream out_occupancy(occupancy_output_file_path, std::fstream::out);
    if (!out_occupancy) {
        std::cerr << "Unable to open file: " << occupancy_output_file_path << '\n';
        exit(EXIT_FAILURE);
    }
    out_occupancy << dim_x << ' ' << dim_y << '\n';

    for (int y = 0; y < dim_y; ++y) {
        for (int x = 0; x < dim_x; ++x) {
            out_occupancy << occupancy[y * dim_x + x] << (x == dim_x - 1 ? "" : " ");
        }
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

    const int step_x = delta_x > 0 ? 1 : -1;
    const int step_y = delta_y > 0 ? 1 : -1;

    if (index < distance_x) {
        const int offset = static_cast<int>(index) + 1;
        wire.mid_x = wire.start_x + step_x * offset;
        wire.mid_y = wire.start_y;
        wire.move_x_start = true;
        wire.move_x_end = true;
    } else if (index < distance_x + distance_y) {
        const int offset = static_cast<int>(index - distance_x) + 1;
        wire.mid_x = wire.start_x;
        wire.mid_y = wire.start_y + step_y * offset;
        wire.move_x_start = false;
        wire.move_x_end = false;
    } else {
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

template <typename Func> void for_each_wire_cell(const Wire& wire, Func visit) {
    auto visit_segment = [&](int start_x, int start_y, int end_x, int end_y) {
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
    visit(wire.end_x, wire.end_y);
}

// 1. Flattened 1D Grid implementation
void add_or_remove_wire_from_occupancy(const Wire& wire, std::vector<int>& occupancy, const int dim_x, const bool add) {
    const int delta = add ? 1 : -1;
    for_each_wire_cell(wire, [&](int x, int y) { occupancy[y * dim_x + x] += delta; });
}

unsigned int test_wire_addition_to_occupancy(const Wire& wire, const std::vector<int>& occupancy, const int dim_x) {
    unsigned int acc = 0;
    for_each_wire_cell(wire, [&](int x, int y) { acc += occupancy[y * dim_x + x]; });
    return acc;
}

constexpr unsigned int TREE_FANOUT = 4;

void send_data(const unsigned int pid, const unsigned int nproc, const Message& to_send,
               std::deque<PendingSend>& pending) {

    if (nproc <= 1)
        return;

    const unsigned int relative = (pid + nproc - to_send.origin) % nproc;
    const unsigned int first_child = TREE_FANOUT * relative + 1;

    if (first_child >= nproc)
        return;

    const unsigned int num_children = std::min(TREE_FANOUT, nproc - first_child);
    const int bytes = static_cast<int>(offsetof(Message, updates) + to_send.num_updates * sizeof(WireUpdate));

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

// Restored MPI_Iprobe/MPI_Recv message polling pattern
unsigned int receive_and_process_data(const unsigned int pid, const unsigned int nproc, std::vector<Wire>& wires,
                                      std::vector<int>& occupancy, const int dim_x,
                                      std::vector<WireUpdate>& best_partial, std::deque<PendingSend>& pending,
                                      bool block = false) {
    unsigned int messages_received = 0;

    while (true) {
        int flag = 0;
        MPI_Status status;

        if (block && messages_received == 0) {
            MPI_Probe(MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
            flag = 1;
            block = false; // Only block on the first read of the loop to prevent deadlock
        } else {
            MPI_Iprobe(MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &flag, &status);
        }

        if (!flag) {
            break;
        }

        Message msg;
        MPI_Recv(&msg, sizeof(Message), MPI_BYTE, status.MPI_SOURCE, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        send_data(pid, nproc, msg, pending);

        for (unsigned int j = 0; j < msg.num_updates; ++j) {
            const WireUpdate& u = msg.updates[j];
            if (u.partial) {
                record_partial(u, best_partial);
            } else {
                Wire& wire = wires[u.wire_id];
                add_or_remove_wire_from_occupancy(wire, occupancy, dim_x, false);
                get_ith_wire(wire, u.route_index);
                add_or_remove_wire_from_occupancy(wire, occupancy, dim_x, true);
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

// 2. Hoist Allocations Out of the Iteration Loop
ProcessTask partition_wire(const unsigned int pid, const unsigned int nproc, const unsigned int iteration,
                           const unsigned int initial_batch_size, std::vector<char>& random_wire,
                           std::vector<Wire>& wires, std::vector<WireTask>& wire_work_units, const unsigned int dim_x,
                           std::vector<unsigned long long>& wire_work, std::vector<uint64_t>& spatial_key,
                           std::vector<unsigned long long>& prefix) {
    ProcessTask task{};
    double split_time_start = get_current_time();

    wire_work_units.clear();

    const unsigned int num_wires = wires.size();

    // Reusing pre-allocated vectors
    for (size_t i = 0; i < wires.size(); i++) {
        bool is_random = random_wire[num_wires * iteration + i];
        wire_work[i] = work_of_wire(wires[i], is_random);
    }

    unsigned long long total_work = std::accumulate(wire_work.begin(), wire_work.end(), 0ULL);

    unsigned long long target =
        std::max(std::max(1ULL, (unsigned long long)(within_wires_threshold * total_work / num_wires)),
                 total_work / (static_cast<unsigned long long>(tasks_per_processor) * nproc));

    for (unsigned int i = 0; i < num_wires; i++) {
        bool is_random = random_wire[num_wires * iteration + i];

        if (is_random) {
            wire_work_units.push_back({i, 0, 0, true});
            continue;
        }

        unsigned int candidates = number_of_possible_wires(wires[i]);

        unsigned int splits =
            nproc == 1 ? 1
                       : std::min<unsigned long long>(candidates, wire_work[i] ? 1 + (wire_work[i] - 1) / target : 1);

        for (unsigned int j = 0; j < splits; j++) {
            unsigned int start = (uint64_t)candidates * j / splits;
            unsigned int end = (uint64_t)candidates * (j + 1) / splits;
            wire_work_units.push_back({i, start, end, false});
        }
    }

    constexpr unsigned int TILE_SIZE = 32;
    const unsigned int tiles_x = (dim_x + TILE_SIZE - 1) / TILE_SIZE;

    // Reusing pre-allocated spatial key vector
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

    const unsigned int total_tasks = wire_work_units.size();
    task.batch_size = initial_batch_size;

    // Assign avoids reallocating if capacity is already sufficient
    prefix.assign(total_tasks + 1, 0);
    for (unsigned int i = 0; i < total_tasks; ++i) {
        const WireTask& wt = wire_work_units[i];
        const unsigned int candidates = number_of_possible_wires(wires[wt.wire_id]);
        prefix[i + 1] =
            prefix[i] + (wt.random ? wire_work[wt.wire_id] : wire_work[wt.wire_id] * (wt.end - wt.start) / candidates);
    }

    auto boundary = [&](unsigned int rank) {
        unsigned long long desired = (prefix.back() / nproc) * rank + (prefix.back() % nproc) * rank / nproc;
        return static_cast<unsigned int>(std::lower_bound(prefix.begin(), prefix.end(), desired) - prefix.begin());
    };

    task.start_task = boundary(pid);
    task.end_task = boundary(pid + 1);

    const unsigned int batches_to_process = (task.end_task - task.start_task + task.batch_size - 1) / task.batch_size;

    unsigned int total_messages = 0;
    for (unsigned int rank = 0; rank < nproc; ++rank) {
        const unsigned int count = boundary(rank + 1) - boundary(rank);
        total_messages += (count + task.batch_size - 1) / task.batch_size;
    }
    task.expected_messages_received_per_iteration = total_messages - batches_to_process;

    double split_time_end = get_current_time();
    task.time_taken_to_partition = split_time_end - split_time_start;

    if (debug) {
        unsigned long long assigned_work = prefix[task.end_task] - prefix[task.start_task];
        printf("pid: %u, total task: %zu, batches_per_processor = %u, batch_size: %u, work: %llu\n", pid,
               wire_work_units.size(), batches_to_process, task.batch_size, assigned_work);
    }

    return task;
}

int main(int argc, char* argv[]) {
    const auto init_start = std::chrono::steady_clock::now();
    int pid;
    int nproc;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &pid);
    MPI_Comm_size(MPI_COMM_WORLD, &nproc);

    std::string input_filename;
    double SA_prob = 0.1;
    int SA_iters = 5;
    int batch_size = 1;

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

    // 1. Switched to 1D Occupancy Grid Memory Allocation
    std::vector<int> occupancy;

    if (pid == 0) {
        std::ifstream fin(input_filename);
        if (!fin) {
            std::cerr << "Unable to open file: " << input_filename << ".\n";
            exit(EXIT_FAILURE);
        }

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

    unsigned int metadata[3] = {dim_x, dim_y, num_wires};
    MPI_Bcast(metadata, 3, MPI_UNSIGNED, 0, MPI_COMM_WORLD);
    dim_x = metadata[0];
    dim_y = metadata[1];
    num_wires = metadata[2];
    wires.resize(num_wires);

    // Allocate 1D space
    occupancy.resize(dim_y * dim_x, 0);

    MPI_Bcast(wires.data(), num_wires * sizeof(Wire), MPI_BYTE, 0, MPI_COMM_WORLD);

    std::vector<char> random_wires(num_wires * SA_iters);
    if (pid == 0) {
        std::mt19937 rng(std::random_device{}());
        std::bernoulli_distribution dist(SA_prob);
        for (auto& x : random_wires)
            x = dist(rng);
    }
    MPI_Bcast(random_wires.data(), random_wires.size(), MPI_CHAR, 0, MPI_COMM_WORLD);

    for (const auto& wire : wires) {
        add_or_remove_wire_from_occupancy(wire, occupancy, dim_x, true);
    }

    if (pid == 0) {
        const double init_time =
            std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - init_start)
                .count();
        std::cout << "Initialization time (sec): " << std::fixed << std::setprecision(10) << init_time << '\n';
    }

    std::deque<PendingSend> pending;
    const auto compute_start = std::chrono::steady_clock::now();

    double split_time = 0.0f, send_time = 0.0f, receive_time = 0.0f;
    double stall_time = 0.0f, route_time = 0.0f, sync_time = 0.0f;
    std::mt19937 rng(std::random_device{}());

    // 2. Pre-allocate hoisted memory vectors outside of the iteration loop
    std::vector<unsigned long long> wire_work_hoisted(num_wires);
    std::vector<uint64_t> spatial_key_hoisted(num_wires);
    std::vector<unsigned long long> prefix_hoisted;

    for (int SA_iterations = 0; SA_iterations < SA_iters; ++SA_iterations) {
        wire_work_units.clear();

        // Pass hoisted arrays
        ProcessTask process_task =
            partition_wire(pid, nproc, SA_iterations, batch_size, random_wires, wires, wire_work_units, dim_x,
                           wire_work_hoisted, spatial_key_hoisted, prefix_hoisted);
        split_time += process_task.time_taken_to_partition;

        unsigned int messages_received = 0;
        std::vector<WireUpdate> best_partial(num_wires, {-1, INT_MAX, UINT_MAX, true});

        for (unsigned int start_task = process_task.start_task; start_task < process_task.end_task;
             start_task += process_task.batch_size) {
            const unsigned int end_task = std::min(start_task + process_task.batch_size, process_task.end_task);
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

                add_or_remove_wire_from_occupancy(wire, occupancy, dim_x, false);
                const unsigned int original_wire_cost =
                    test_wire_addition_to_occupancy(original_wire, occupancy, dim_x);
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
                        const unsigned int cost = test_wire_addition_to_occupancy(candidate, occupancy, dim_x);
                        if (cost < min_cost) {
                            min_cost = cost;
                            best_wire_index_inner = j;
                        }
                    }

                    if (partial) {
                        WireUpdate update{static_cast<int>(task.wire_id), static_cast<int>(best_wire_index_inner),
                                          min_cost < original_wire_cost ? min_cost : UINT_MAX, true};
                        msg.updates[msg.num_updates++] = update;
                        record_partial(update, best_partial);
                    } else if (min_cost < original_wire_cost) {
                        best_wire_index = best_wire_index_inner;
                        original_wire_better = false;
                    }
                }

                if (partial || original_wire_better) {
                    wire = original_wire;
                } else {
                    get_ith_wire(wire, best_wire_index);
                    msg.updates[msg.num_updates++] = {static_cast<int>(task.wire_id), static_cast<int>(best_wire_index),
                                                      0, false};
                }

                add_or_remove_wire_from_occupancy(wire, occupancy, dim_x, true);
                const double route_end_time = get_current_time();
                route_time += route_end_time - route_start_time;
                if ((i - start_task + 1) % 4 == 0 && i + 1 != end_task) {
                const double receive_start_time = get_current_time();
                messages_received +=
                    receive_and_process_data(pid, nproc, wires, occupancy, dim_x, best_partial, pending, false);
                receive_time += get_current_time() - receive_start_time;
                }
            }

            const double send_start_time = get_current_time();
            send_data(pid, nproc, msg, pending);
            cleanup_pending_sends(pending);
            const double send_end_time = get_current_time();
            send_time += send_end_time - send_start_time;

            const double receive_start_time = get_current_time();
            messages_received +=
                receive_and_process_data(pid, nproc, wires, occupancy, dim_x, best_partial, pending, false);
            receive_time += get_current_time() - receive_start_time;
        }

        const double sync_start_time = get_current_time();

        // Pass block = true to use MPI_Probe and avoid spinning the CPU when strictly waiting
        while (messages_received < process_task.expected_messages_received_per_iteration) {
            messages_received +=
                receive_and_process_data(pid, nproc, wires, occupancy, dim_x, best_partial, pending, true);
        }

        for (unsigned int wire_id = 0; wire_id < num_wires; ++wire_id) {
            const WireUpdate& best = best_partial[wire_id];
            if (best.cost == UINT_MAX)
                continue;
            Wire& wire = wires[wire_id];
            add_or_remove_wire_from_occupancy(wire, occupancy, dim_x, false);
            get_ith_wire(wire, static_cast<unsigned int>(best.route_index));
            add_or_remove_wire_from_occupancy(wire, occupancy, dim_x, true);
        }

        const double sync_end_time = get_current_time();
        sync_time += sync_end_time - sync_start_time;
        MPI_Barrier(MPI_COMM_WORLD);
        const double stall_end_time = get_current_time();
        stall_time += stall_end_time - sync_end_time;
    }

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
        // Rebuild temporary 2D grid exclusively for wr_checker compilation support
        std::vector<std::vector<int>> occ_2d(dim_y, std::vector<int>(dim_x));
        for (unsigned int y = 0; y < dim_y; ++y) {
            for (unsigned int x = 0; x < dim_x; ++x) {
                occ_2d[y][x] = occupancy[y * dim_x + x];
            }
        }
        wr_checker checker(wires, occ_2d);
        checker.validate();

        print_stats(occupancy, dim_x, dim_y);
        write_output(wires, num_wires, occupancy, dim_x, dim_y);
    }

    if (debug) {
        if (std::getenv("WR_RANK_TIMINGS")) {
            const double local_times[] = {route_time, split_time, send_time, receive_time, sync_time, stall_time};
            std::vector<double> rank_times(pid == 0 ? 6 * nproc : 0);
            MPI_Gather(local_times, 6, MPI_DOUBLE, rank_times.data(), 6, MPI_DOUBLE, 0, MPI_COMM_WORLD);
            if (pid == 0) {
                for (int rank = 0; rank < nproc; ++rank) {
                    const double* t = rank_times.data() + 6 * rank;
                    printf("Rank timing: rank=%d route=%.6f split=%.6f send=%.6f receive=%.6f sync=%.6f stall=%.6f\n",
                           rank, t[0], t[1], t[2], t[3], t[4], t[5]);
                }
            }
        }

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