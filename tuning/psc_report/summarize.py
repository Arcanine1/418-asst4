#!/usr/bin/env python3
"""Export Q3 tables and optional figures from a completed frozen campaign."""
import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path
from statistics import median
import subprocess
import sys

from common import CURVE_BATCHES, cases_for, load_campaign


def write_csv(path, rows, fields):
    with path.open("w", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    parser.add_argument("--ghc-dir", type=Path, help="Q2 directory containing timing_summary.csv and manifest.json.")
    parser.add_argument("--plots", action="store_true")
    parser.add_argument("--allow-partial", action="store_true", help="Inspect available data before all jobs finish.")
    args = parser.parse_args()
    campaign, manifest = load_campaign(args.campaign)
    groups, records, incomplete = defaultdict(list), [], []
    for job in manifest["jobs"]:
        directory = campaign / "jobs" / job["name"]
        for repeat in range(1, manifest["config"]["repeats"] + 1):
            for name, batch, ranks in cases_for(job, manifest["config"]):
                path = directory / "runs" / f"{name}-b{batch}-n{ranks}-r{repeat}" / "result.json"
                if not path.exists():
                    incomplete.append(str(path.relative_to(campaign)))
                    continue
                row = json.loads(path.read_text())
                expected = (job["name"], name, batch, ranks, repeat, job["nodes"])
                actual = tuple(row[key] for key in ("job", "input", "batch", "ranks", "repeat", "nodes"))
                if actual != expected:
                    raise ValueError(f"Run identity differs from plan: {path}")
                records.append(row)
                groups[(job["name"], name, batch, ranks)].append(row)
    if incomplete and not args.allow_partial:
        raise SystemExit(f"{len(incomplete)} runs are missing. Wait for all jobs or use --allow-partial. "
                         f"First missing: {incomplete[0]}")
    if not records:
        raise SystemExit("No completed routing measurements.")
    summaries = []
    refs = {"few": 29240, "medium": 267589, "abundant": 1539198}
    for (job_name, name, batch, ranks), values in sorted(groups.items()):
        row = {key: values[0][key] for key in
               ("job", "suite", "nodes", "ranks_per_node", "input", "batch", "ranks")}
        row["runs"] = len(values)
        for metric in ("computation_sec", "total_sec", "initialization_sec", "init_plus_compute_sec", "cost"):
            samples = [value[metric] for value in values]
            row[metric + "_median"] = median(samples)
            row[metric + "_min"] = min(samples)
            row[metric + "_max"] = max(samples)
        row["costs_within_5pct"] = all(abs(value["cost"] / refs[name] - 1) <= .05 for value in values)
        summaries.append(row)
    baselines = {(r["input"], r["batch"]): r for r in summaries if r["suite"] == "single" and r["ranks"] == 1}
    for row in summaries:
        baseline = baselines.get((row["input"], row["batch"]))
        if baseline:
            row["computation_speedup"] = baseline["computation_sec_median"] / row["computation_sec_median"]
            row["total_speedup"] = baseline["total_sec_median"] / row["total_sec_median"]
    fields = ["job", "suite", "input", "batch", "nodes", "ranks_per_node", "ranks", "runs",
              "computation_sec_median", "computation_sec_min", "computation_sec_max",
              "total_sec_median", "total_sec_min", "total_sec_max", "initialization_sec_median",
              "init_plus_compute_sec_median", "computation_speedup", "total_speedup",
              "cost_median", "cost_min", "cost_max", "costs_within_5pct"]
    write_csv(campaign / "runs.csv", records, [
        "job", "suite", "input", "batch", "nodes", "ranks_per_node", "ranks", "repeat",
        "slurm_job_id", "computation_sec", "total_sec", "initialization_sec", "init_plus_compute_sec", "cost", "max_occupancy"])
    write_csv(campaign / "summary.csv", summaries, fields)
    single = [r for r in summaries if r["suite"] == "single"]
    write_csv(campaign / "q3a_speedups.csv", [r for r in single if r["batch"] in CURVE_BATCHES], fields)
    write_csv(campaign / "q3c_batches.csv", [r for r in single if r["ranks"] == 32], fields)

    layouts = []
    by_layout = {(r["input"], r["nodes"], r["ranks_per_node"]): r
                 for r in summaries if r["suite"] == "layout"}
    for name in manifest["config"]["inputs"]:
        for per_node in (2, 8, 64):
            one = by_layout.get((name, 1, per_node))
            two = by_layout.get((name, 2, per_node))
            if not one or not two:
                continue
            row = {"input": name, "batch": 16, "ranks_per_node": per_node,
                   "one_node_ranks": per_node, "two_node_ranks": 2 * per_node}
            for prefix, source in (("one_node", one), ("two_node", two)):
                for field in ("runs", "computation_sec_median", "total_sec_median", "cost_median"):
                    row[prefix + "_" + field] = source[field]
            row["node_doubling_computation_speedup"] = one["computation_sec_median"] / two["computation_sec_median"]
            row["node_doubling_total_speedup"] = one["total_sec_median"] / two["total_sec_median"]
            layouts.append(row)
    layout_fields = ["input", "batch", "ranks_per_node", "one_node_ranks", "two_node_ranks",
                     "one_node_runs", "two_node_runs", "one_node_computation_sec_median",
                     "two_node_computation_sec_median", "one_node_total_sec_median", "two_node_total_sec_median",
                     "one_node_cost_median", "two_node_cost_median",
                     "node_doubling_computation_speedup", "node_doubling_total_speedup"]
    write_csv(campaign / "q3d_nodes.csv", layouts, layout_fields)

    comparison = []
    if args.ghc_dir:
        ghc = args.ghc_dir.resolve()
        ghc_manifest = json.loads((ghc / "manifest.json").read_text())
        if ghc_manifest.get("launch_policy") != "plain-mpirun" or ghc_manifest.get("mpi_flags"):
            raise ValueError("GHC comparison data must use the allowed plain-mpirun configuration.")
        ghc_hash = ghc_manifest["source_hashes"]["source/wireroute.cpp"]
        if ghc_hash != manifest["source_sha256"]:
            raise ValueError("GHC and PSC source hashes differ. Use measurements of the same source version.")
        with (ghc / "timing_summary.csv").open(newline="") as source:
            ghc_rows = {(r["input"], int(r["batch"]), int(r["ranks"])): r for r in csv.DictReader(source)}
        for row in single:
            if row["batch"] not in CURVE_BATCHES or row["ranks"] not in (1, 2, 4, 8):
                continue
            key = (row["input"], row["batch"], row["ranks"])
            other = ghc_rows.get(key)
            if not other:
                raise ValueError(f"GHC data missing common-rank comparison: {key}")
            result = {key: row[key] for key in ("input", "batch", "ranks")}
            for metric in ("computation_sec_median", "total_sec_median", "computation_speedup", "total_speedup"):
                result["psc_" + metric] = row[metric]
                result["ghc_" + metric] = float(other[metric])
            comparison.append(result)
    compare_fields = ["input", "batch", "ranks", "psc_computation_sec_median", "ghc_computation_sec_median",
                      "psc_total_sec_median", "ghc_total_sec_median", "psc_computation_speedup",
                      "ghc_computation_speedup", "psc_total_speedup", "ghc_total_speedup"]
    write_csv(campaign / "q3b_ghc_comparison.csv", comparison, compare_fields)
    lines = [
        "Launch policy: plain mpirun -np N",
        f"Source: {manifest['source_sha256']}",
        f"Completed runs: {len(records)}; missing: {len(incomplete)}",
        "Total time includes MPI startup/teardown, validation and shared-filesystem output.",
        "Speedups use the same input/batch one-rank baseline. Values are medians across repeats.",
        "Q3(d) doubles total ranks when changing from one node to two at fixed ranks per node.",
        "GHC total-time comparisons may include a different output filesystem; see the Q2 manifest.",
        "Q3(b): " + ("comparison CSV created" if comparison else "pending --ghc-dir with completed plain-MPI Q2 data"),
        "", "input batch ranks nodes compute_sec total_sec compute_speedup cost_median",
    ]
    for row in summaries:
        speedup = f"{row['computation_speedup']:.2f}x" if "computation_speedup" in row else "NA"
        lines.append(f"{row['input']} {row['batch']} {row['ranks']} {row['nodes']} "
                     f"{row['computation_sec_median']:.6f} {row['total_sec_median']:.6f} "
                     f"{speedup} {row['cost_median']:.0f}")
    text = "\n".join(lines) + "\n"
    (campaign / "summary.txt").write_text(text)
    print(text, end="", flush=True)
    if args.plots:
        subprocess.run([sys.executable, str(Path(__file__).with_name("plot.py")), str(campaign)], check=True)


if __name__ == "__main__":
    main()
