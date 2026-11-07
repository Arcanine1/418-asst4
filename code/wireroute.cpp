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
const unsigned int tasks_per_processor = 4;
const float within_wires_threshold = .1;
constexpr int REDUCTION_MIN_RANKS = 64;
constexpr unsigned int TREE_FANOUT = 4;
// Occupancy counts use one byte; the supported counts are 0 through 255.
using Occupancy = std::vector<std::vector<uint8_t>>;

// One context per MPI rank, shared with helpers through a const reference.
struct RunContext {
    int pid = 0;
    int nproc = 1;
    std::string input_filename;
    double SA_prob = 0.1;
    int SA_iters = 5;
    int iteration = 0;
    int batch_size = 1;
    unsigned int dim_x = 0;
    unsigned int dim_y = 0;
    unsigned int num_wires = 0;
    bool reduce_mode = false;
};

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
    unsigned long long assigned_work;
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

void add_or_remove_wire_from_occupancy(const Wire& wire, Occupancy& occupancy, const bool add) {
    const int delta = add ? 1 : -1;
    for_each_wire_cell(wire, [&](int x, int y) { occupancy[y][x] += delta; });
}

// Returns the total occupancy of the wire if it were to be added to the occupancy grid but not the exact cost. Instead
// we ensure that the correct ordering is met between wires
unsigned int test_wire_addition_to_occupancy(const Wire& wire, Occupancy& occupancy) {
    unsigned int acc = 0;
    for_each_wire_cell(wire, [&](int x, int y) { acc += occupancy[y][x]; });
    return acc;
}

// Handle sending new data, forwarding data, and handling pending sends cleanup.

void send_data(const RunContext& run, const Message& to_send, std::deque<PendingSend>& pending) {
    const unsigned int pid = run.pid;
    const unsigned int nproc = run.nproc;

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

unsigned int receive_and_process_data(const RunContext& run, std::vector<Wire>& wires, Occupancy& occupancy,
                                      std::vector<WireUpdate>& best_partial, std::deque<PendingSend>& pending) {
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
        assert(msg.origin < static_cast<unsigned int>(run.nproc));
        assert(msg.num_updates <= MAX_UPDATES_PER_MESSAGE);
        // Forward each received batch exactly once along its origin's tree.
        send_data(run, msg, pending);
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

unsigned long long work_of_wire(const Wire& wire, bool is_random) {
    const unsigned int wire_length_cost =
        std::abs(wire.end_x - wire.start_x) + 2 * std::abs(wire.end_y - wire.start_y) + 1;
    if (is_random) {
        return wire_length_cost;
    } else {
        return static_cast<unsigned long long>(number_of_possible_wires(wire)) * wire_length_cost;
    }
}

// Returns wires in a spatial manner using a snake pattern
std::vector<unsigned int> spatial_wire_order(const std::vector<Wire>& wires, unsigned int dim_x) {
    constexpr unsigned int tile_size = 32;
    const unsigned int tiles_x = (dim_x + tile_size - 1) / tile_size;
    std::vector<uint64_t> keys(wires.size());
    std::vector<unsigned int> order(wires.size());
    std::iota(order.begin(), order.end(), 0u);
    for (unsigned int i = 0; i < wires.size(); ++i) {
        const Wire& w = wires[i];
        const unsigned int mid_x = (static_cast<unsigned int>(w.start_x) + w.end_x) / 2;
        const unsigned int mid_y = (static_cast<unsigned int>(w.start_y) + w.end_y) / 2;
        const unsigned int tile_x = mid_x / tile_size, tile_y = mid_y / tile_size;
        const unsigned int ordered_x = tile_y % 2 == 0 ? tile_x : tiles_x - 1 - tile_x;
        keys[i] = static_cast<uint64_t>(tile_y) * tiles_x + ordered_x;
    }
    std::sort(order.begin(), order.end(),
              [&](unsigned int a, unsigned int b) { return keys[a] != keys[b] ? keys[a] < keys[b] : a < b; });
    return order;
}

ProcessTask partition_wire(const RunContext& run, std::vector<char>& random_wire, std::vector<Wire>& wires,
                           std::vector<WireTask>& wire_work_units, const std::vector<unsigned int>& wire_order,
                           const std::vector<double>& rank_weights) {
    const unsigned int nproc = run.nproc;
    ProcessTask task{};
    wire_work_units.clear();

    // Estimate work per wire
    const unsigned int num_wires = wires.size();
    std::vector<unsigned long long> wire_work(num_wires);

    for (size_t i = 0; i < wires.size(); i++) {
        bool is_random = random_wire[num_wires * run.iteration + i];
        wire_work[i] = work_of_wire(wires[i], is_random);
    }

    unsigned long long total_work = std::accumulate(wire_work.begin(), wire_work.end(), 0ULL);

    // Split only enough to balance several tasks per rank. Excess partial
    // tasks multiply all-rank messages and defer otherwise local route updates.
    const unsigned int partition_multiplier =
        64 <= nproc && 1024 <= num_wires && num_wires <= 4096 && run.batch_size == 16 ? 16 : tasks_per_processor;
    unsigned long long target =
        std::max(std::max(1ULL, (unsigned long long)(within_wires_threshold * total_work / num_wires)),
                 total_work / (static_cast<unsigned long long>(partition_multiplier) * nproc));

    // Split wires into tasks
    for (unsigned int i : wire_order) {
        bool is_random = random_wire[num_wires * run.iteration + i];

        if (is_random) {
            wire_work_units.push_back({i, 0, 0, true});
            continue;
        }

        unsigned int candidates = number_of_possible_wires(wires[i]);

        unsigned int splits =
            std::min<unsigned long long>(candidates, wire_work[i] ? 1 + (wire_work[i] - 1) / target : 1);

        for (unsigned int j = 0; j < splits; j++) {
            unsigned int start = (uint64_t)candidates * j / splits;
            unsigned int end = (uint64_t)candidates * (j + 1) / splits;
            wire_work_units.push_back({i, start, end, false});
        }
    }

    // Balance individual tasks; batch size controls only update propagation.
    const unsigned int total_tasks = wire_work_units.size();
    task.batch_size = run.batch_size;
    std::vector<unsigned long long> prefix(total_tasks + 1, 0);
    for (unsigned int i = 0; i < total_tasks; ++i) {
        const WireTask& wt = wire_work_units[i];
        const unsigned int candidates = number_of_possible_wires(wires[wt.wire_id]);
        prefix[i + 1] =
            prefix[i] + (wt.random ? wire_work[wt.wire_id] : wire_work[wt.wire_id] * (wt.end - wt.start) / candidates);
    }

    // Give each rank work in proportion to its measured routing rate.
    const double total_weight = std::accumulate(rank_weights.begin(), rank_weights.end(), 0.0);
    std::vector<double> weight_prefix(nproc + 1, 0.0);
    for (unsigned int rank = 0; rank < nproc; ++rank)
        weight_prefix[rank + 1] = weight_prefix[rank] + rank_weights[rank];
    auto boundary = [&](unsigned int rank) {
        unsigned long long desired = rank == nproc
                                         ? prefix.back()
                                         : static_cast<unsigned long long>(static_cast<double>(prefix.back()) *
                                                                           weight_prefix[rank] / total_weight);

        return static_cast<unsigned int>(std::lower_bound(prefix.begin(), prefix.end(), desired) - prefix.begin());
    };

    task.start_task = boundary(run.pid);
    task.end_task = boundary(run.pid + 1);
    task.assigned_work = prefix[task.end_task] - prefix[task.start_task];

    const unsigned int batches_to_process = (task.end_task - task.start_task + task.batch_size - 1) / task.batch_size;

    // Each rank flushes its last, possibly short batch. Count those messages too.
    unsigned int total_messages = 0;
    for (unsigned int rank = 0; rank < nproc; ++rank) {
        const unsigned int count = boundary(rank + 1) - boundary(rank);
        total_messages += (count + task.batch_size - 1) / task.batch_size;
    }
    task.expected_messages_received_per_iteration = total_messages - batches_to_process;

    return task;
}

struct RouteProposal {
    int cost;
    int index;
};
static_assert(sizeof(RouteProposal) == 2 * sizeof(int));

struct ReductionContext {
    MPI_Comm group = MPI_COMM_NULL;
    MPI_Comm leaders = MPI_COMM_NULL;
    int local_rank = 0;
};

// Found that these compiler flags helped generate better assembly
[[gnu::flatten, gnu::noinline]]
void route_with_reduction(const RunContext& run, const ProcessTask& process_task, const std::vector<WireTask>& tasks,
                          std::vector<Wire>& wires, Occupancy& occupancy, std::mt19937& rng, double& route_time,
                          std::vector<double>& rank_weights, const ReductionContext& reduction) {
    const size_t num_wires = wires.size();
    const bool update_weights = run.iteration + 1 < run.SA_iters;
    const double route_time_before = route_time;
    constexpr RouteProposal no_proposal{INT_MAX, INT_MAX};

    // Append one rate entry per rank when another iteration needs load balancing.
    const size_t num_rate_entries = update_weights ? run.nproc : 0;
    std::vector<RouteProposal> proposals(num_wires + num_rate_entries, no_proposal);

    for (unsigned int i = process_task.start_task; i < process_task.end_task; ++i) {
        const double task_start = MPI_Wtime();
        const WireTask& task = tasks[i];
        const Wire original_wire = wires[task.wire_id];

        // Score routes without counting this wire's own occupancy.
        add_or_remove_wire_from_occupancy(original_wire, occupancy, false);
        const unsigned int original_cost = test_wire_addition_to_occupancy(original_wire, occupancy);
        RouteProposal best_for_task = no_proposal;

        if (task.random) {
            std::uniform_int_distribution<int> random_route(0, number_of_possible_wires(original_wire) - 1);
            // Random moves must survive MINLOC even when they increase the cost.
            best_for_task = {-1, random_route(rng)};
        } else {
            Wire candidate = original_wire;
            for (unsigned int j = task.start; j < task.end; ++j) {
                get_ith_wire(candidate, j);
                const unsigned int cost = test_wire_addition_to_occupancy(candidate, occupancy);
                if (cost < original_cost && cost < static_cast<unsigned int>(best_for_task.cost)) {
                    best_for_task = {static_cast<int>(cost), static_cast<int>(j)};
                }
            }
        }
        add_or_remove_wire_from_occupancy(original_wire, occupancy, true);

        // A wire can have several tasks. Keep its best proposal on this rank.
        RouteProposal& best_for_wire = proposals[task.wire_id];
        if (best_for_task.cost < best_for_wire.cost ||
            (best_for_task.cost == best_for_wire.cost && best_for_task.index < best_for_wire.index)) {
            best_for_wire = best_for_task;
        }
        route_time += MPI_Wtime() - task_start;
    }

    if (update_weights) {
        const double elapsed = std::max(route_time - route_time_before, 1e-9);
        const double scaled_rate = static_cast<double>(process_task.assigned_work) / elapsed / 1024.0;
        const int rate = static_cast<int>(std::clamp(scaled_rate, 1.0, double(INT_MAX - 1)));

        // Only this rank writes this slot; the other ranks leave it at INT_MAX.
        proposals[num_wires + run.pid] = {0, rate};
    }

    // Find the winning route for each wire and distribute all ranks' rates.
    assert(reduction.group != MPI_COMM_NULL);
    const int count = static_cast<int>(proposals.size());
    const bool group_leader = reduction.local_rank == 0;
    MPI_Reduce(group_leader ? MPI_IN_PLACE : proposals.data(), group_leader ? proposals.data() : nullptr, count,
               MPI_2INT, MPI_MINLOC, 0, reduction.group);
    if (group_leader) {
        MPI_Allreduce(MPI_IN_PLACE, proposals.data(), count, MPI_2INT, MPI_MINLOC, reduction.leaders);
    }
    MPI_Bcast(proposals.data(), count, MPI_2INT, 0, reduction.group);

    // Every rank applies the same winners in the same order.
    for (size_t wire_id = 0; wire_id < num_wires; ++wire_id) {
        const RouteProposal& winner = proposals[wire_id];
        if (winner.cost == INT_MAX) {
            continue;
        }
        Wire& wire = wires[wire_id];
        add_or_remove_wire_from_occupancy(wire, occupancy, false);
        get_ith_wire(wire, static_cast<unsigned int>(winner.index));
        add_or_remove_wire_from_occupancy(wire, occupancy, true);
    }

    if (update_weights) {
        double average_rate = 0;
        for (int rank = 0; rank < run.nproc; ++rank) {
            average_rate += proposals[num_wires + rank].index;
        }
        average_rate /= run.nproc;

        // Smooth the weights so one noisy timing does not move too much work.
        for (int rank = 0; rank < run.nproc; ++rank) {
            const double relative_rate = proposals[num_wires + rank].index / average_rate;
            const double target_weight = std::clamp(relative_rate, 0.6, 1.4);
            rank_weights[rank] = 0.5 * rank_weights[rank] + 0.5 * target_weight;
        }
    }
}

// Route one task and collect its updates for the next batch message.
// Found that these compiler flags helped generate better assembly
[[gnu::flatten, gnu::noinline]]
void route_task(const WireTask& task, std::vector<Wire>& wires, Occupancy& occupancy, std::mt19937& rng, Message& msg,
                std::vector<WireUpdate>& best_partial) {
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
        msg.updates[msg.num_updates++] = {static_cast<int>(task.wire_id), static_cast<int>(best_wire_index), 0, false};
    }

    add_or_remove_wire_from_occupancy(wire, occupancy, true);
}

int main(int argc, char* argv[]) {
    const auto init_start = std::chrono::steady_clock::now();
    RunContext run;

    // Initialize MPI
    MPI_Init(&argc, &argv);
    // Get process rank
    MPI_Comm_rank(MPI_COMM_WORLD, &run.pid);
    // Get total number of processes
    MPI_Comm_size(MPI_COMM_WORLD, &run.nproc);

    // Read command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "f:p:i:m:b:")) != -1) {
        switch (opt) {
        case 'f':
            run.input_filename = optarg;
            break;
        case 'p':
            run.SA_prob = atof(optarg);
            break;
        case 'i':
            run.SA_iters = atoi(optarg);
            break;
        case 'b':
            run.batch_size = atoi(optarg);
            break;
        default:
            if (run.pid == 0) {
                std::cerr << "Usage: " << argv[0] << " -f input_filename [-p SA_prob] [-i SA_iters] -b batch_size\n";
            }

            MPI_Finalize();
            exit(EXIT_FAILURE);
        }
    }

    // Check if required options are provided
    if (empty(run.input_filename) || run.SA_iters <= 0 || run.batch_size <= 0 ||
        run.batch_size > MAX_UPDATES_PER_MESSAGE) {
        if (run.pid == 0) {
            std::cerr << "Usage: " << argv[0] << " -f input_filename [-p SA_prob] [-i SA_iters] -b batch_size\n";
        }

        MPI_Finalize();
        exit(EXIT_FAILURE);
    }

    if (run.pid == 0) {
        std::cout << "Number of processes: " << run.nproc << '\n';
        std::cout << "Simulated annealing probability parameter: " << run.SA_prob << '\n';
        std::cout << "Simulated annealing iterations: " << run.SA_iters << '\n';
        std::cout << "Input file: " << run.input_filename << '\n';
        std::cout << "Batch size: " << run.batch_size << '\n';
    }

    std::vector<Wire> wires;
    std::vector<WireTask> wire_work_units;
    Occupancy occupancy;

    if (run.pid == 0) {
        std::ifstream fin(run.input_filename);

        if (!fin) {
            std::cerr << "Unable to open file: " << run.input_filename << ".\n";
            exit(EXIT_FAILURE);
        }

        /* Read the grid dimension and wire information from file */
        fin >> run.dim_x >> run.dim_y >> run.num_wires;

        wires.resize(run.num_wires);
        for (auto& wire : wires) {
            fin >> wire.start_x >> wire.start_y >> wire.end_x >> wire.end_y;
            wire.move_x_start = true;
            wire.move_x_end = false;
            wire.mid_x = wire.end_x;
            wire.mid_y = wire.start_y;
        }
    }

    // Shares metadata. Use one bcast to save on communication overhead
    unsigned int metadata[3] = {run.dim_x, run.dim_y, run.num_wires};
    MPI_Bcast(metadata, 3, MPI_UNSIGNED, 0, MPI_COMM_WORLD);
    run.dim_x = metadata[0];
    run.dim_y = metadata[1];
    run.num_wires = metadata[2];
    wires.resize(run.num_wires);
    occupancy.resize(run.dim_y, std::vector<uint8_t>(run.dim_x));

    // Share wires. Use one bcast to save on communication overhead
    MPI_Bcast(wires.data(), run.num_wires * sizeof(Wire), MPI_BYTE, 0, MPI_COMM_WORLD);

    // Rank 0 generates random decisions for everyone so that we have better load balancing
    std::vector<char> random_wires(run.num_wires * run.SA_iters);

    if (run.pid == 0) {
        std::mt19937 rng(std::random_device{}());
        std::bernoulli_distribution dist(run.SA_prob);

        for (auto& x : random_wires)
            x = dist(rng);
    }

    MPI_Bcast(random_wires.data(), random_wires.size(), MPI_CHAR, 0, MPI_COMM_WORLD);

    for (const auto& wire : wires) {
        add_or_remove_wire_from_occupancy(wire, occupancy, true);
    }

    /* Initialize any additional data structures needed in the algorithm */
    const auto wire_order = spatial_wire_order(wires, run.dim_x);
    ReductionContext reduction_context;
    run.reduce_mode = run.nproc >= REDUCTION_MIN_RANKS && run.num_wires <= 4096 && run.batch_size == 16;
    const int group_size = std::clamp(run.num_wires <= 512 ? 8 : 16, 1, run.nproc);
    if (group_size > 1 && run.reduce_mode) {
        MPI_Comm_split(MPI_COMM_WORLD, run.pid / group_size, run.pid, &reduction_context.group);
        MPI_Comm_rank(reduction_context.group, &reduction_context.local_rank);
        MPI_Comm_split(MPI_COMM_WORLD, reduction_context.local_rank == 0 ? 0 : MPI_UNDEFINED, run.pid,
                       &reduction_context.leaders);
    }
    if (run.pid == 0) {
        const double init_time =
            std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - init_start)
                .count();
        std::cout << "Initialization time (sec): " << std::fixed << std::setprecision(10) << init_time << '\n';
    }

    // All messages which are currently pending to be sent. Keep track of them and free the memory when the message
    // fully sends
    std::deque<PendingSend> pending;

    const auto compute_start = std::chrono::steady_clock::now();

    // Routing time feeds the adaptive work distribution for the next iteration.
    double route_time = 0.0;
    std::mt19937 rng(std::random_device{}());
    std::vector<double> rank_weights(run.nproc, 1.0);

    for (run.iteration = 0; run.iteration < run.SA_iters; ++run.iteration) {
        const double route_before = route_time;
        wire_work_units.clear();
        ProcessTask process_task = partition_wire(run, random_wires, wires, wire_work_units, wire_order, rank_weights);
        if (run.reduce_mode) {
            route_with_reduction(run, process_task, wire_work_units, wires, occupancy, rng, route_time, rank_weights,
                                 reduction_context);
        } else {
            unsigned int messages_received = 0;
            std::vector<WireUpdate> best_partial(run.num_wires, {-1, INT_MAX, UINT_MAX, true});

            for (unsigned int start_task = process_task.start_task; start_task < process_task.end_task;
                 start_task += process_task.batch_size) {
                const unsigned int end_task = std::min(start_task + process_task.batch_size, process_task.end_task);
                Message msg{};
                msg.origin = run.pid;
                msg.num_updates = 0;
                for (unsigned int i = start_task; i < end_task; i++) {
                    const double route_start_time = MPI_Wtime();
                    const WireTask& task = wire_work_units[i];
                    route_task(task, wires, occupancy, rng, msg, best_partial);
                    const double route_end_time = MPI_Wtime();
                    route_time += route_end_time - route_start_time;

                    messages_received += receive_and_process_data(run, wires, occupancy, best_partial, pending);
                }

                send_data(run, msg, pending);
                cleanup_pending_sends(pending);
            }

            while (messages_received < process_task.expected_messages_received_per_iteration) {
                messages_received += receive_and_process_data(run, wires, occupancy, best_partial, pending);
            }
            // Every rank now has all partial proposals. Resolve them identically,
            // breaking ties by the lowest candidate index.
            for (unsigned int wire_id = 0; wire_id < run.num_wires; ++wire_id) {
                const WireUpdate& best = best_partial[wire_id];
                if (best.cost == UINT_MAX)
                    continue;
                Wire& wire = wires[wire_id];
                add_or_remove_wire_from_occupancy(wire, occupancy, false);
                get_ith_wire(wire, static_cast<unsigned int>(best.route_index));
                add_or_remove_wire_from_occupancy(wire, occupancy, true);
            }
        }
        if (!run.reduce_mode)
            MPI_Barrier(MPI_COMM_WORLD);
        if (!run.reduce_mode && run.nproc > 1 && run.iteration + 1 < run.SA_iters) {
            const double route_elapsed = std::max(route_time - route_before, 1e-9);
            const double rate = static_cast<double>(process_task.assigned_work) / route_elapsed;
            std::vector<double> rates(run.nproc);
            MPI_Allgather(&rate, 1, MPI_DOUBLE, rates.data(), 1, MPI_DOUBLE, MPI_COMM_WORLD);
            const double average_rate = std::accumulate(rates.begin(), rates.end(), 0.0) / run.nproc;
            for (int rank = 0; rank < run.nproc; ++rank) {
                const double target = std::clamp(rates[rank] / average_rate, 0.6, 1.4);
                rank_weights[rank] = 0.5 * rank_weights[rank] + 0.5 * target;
            }
        }
    }

    // Ensure every nonblocking send completes before freeing its buffer / finalizing MPI.
    for (auto& send : pending) {
        MPI_Waitall(static_cast<int>(send.requests.size()), send.requests.data(), MPI_STATUSES_IGNORE);
    }

    pending.clear();
    if (run.pid == 0) {
        const double compute_time =
            std::chrono::duration_cast<std::chrono::duration<double>>(std::chrono::steady_clock::now() - compute_start)
                .count();
        std::cout << "Computation time (sec): " << std::fixed << std::setprecision(10) << compute_time << '\n';
    }

    if (run.pid == 0) {
        std::vector<std::vector<int>> occupancy_for_output(run.dim_y, std::vector<int>(run.dim_x));
        for (unsigned int y = 0; y < run.dim_y; ++y)
            for (unsigned int x = 0; x < run.dim_x; ++x)
                occupancy_for_output[y][x] = occupancy[y][x];
        wr_checker checker(wires, occupancy_for_output);
        checker.validate();

        /* Write wires and occupancy matrix to files */
        print_stats(occupancy_for_output);
        write_output(wires, run.num_wires, occupancy_for_output, run.dim_x, run.dim_y);
    }

    // Cleanup
    if (reduction_context.leaders != MPI_COMM_NULL)
        MPI_Comm_free(&reduction_context.leaders);
    if (reduction_context.group != MPI_COMM_NULL)
        MPI_Comm_free(&reduction_context.group);
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
