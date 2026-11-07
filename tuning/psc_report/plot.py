#!/usr/bin/env python3
"""Render Q3 CSVs as standalone PNG/PDF figures; never launches MPI."""
import argparse
import csv
import json
from pathlib import Path


def read_csv(path):
    with path.open(newline="") as source:
        return list(csv.DictReader(source))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    args = parser.parse_args()
    campaign = args.campaign.resolve()
    manifest = json.loads((campaign / "campaign.json").read_text())
    if manifest.get("launch_policy") != "plain-mpirun":
        raise SystemExit("Report figures require plain-mpirun measurements.")
    try:
        import matplotlib
    except ImportError:
        raise SystemExit("Plotting requires matplotlib. CSVs are already saved; render on a computer with matplotlib installed.")
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    output = campaign / "figures"
    output.mkdir(exist_ok=True)
    colors = {1: "#0072B2", 4: "#D55E00", 16: "#009E73"}
    curves = read_csv(campaign / "q3a_speedups.csv")
    batches = read_csv(campaign / "q3c_batches.csv")
    nodes = read_csv(campaign / "q3d_nodes.csv")
    comparison = read_csv(campaign / "q3b_ghc_comparison.csv")
    plt.rcParams.update({"font.size": 10, "axes.spines.top": False, "axes.spines.right": False})

    def finish(figure, name):
        if manifest.get("test_data"):
            figure.text(.5, .5, "TEST DATA", ha="center", fontsize=32, alpha=.3, rotation=25)
        figure.tight_layout()
        figure.savefig(output / (name + ".png"), dpi=180, bbox_inches="tight")
        figure.savefig(output / (name + ".pdf"), bbox_inches="tight")
        plt.close(figure)

    for name in manifest["config"]["inputs"]:
        rows = [r for r in curves if r["input"] == name]
        if rows:
            fig, axes = plt.subplots(1, 2, figsize=(10, 4))
            for ax, metric, title in zip(axes, ("total_speedup", "computation_speedup"),
                                         ("Total program speedup", "Computation speedup")):
                ranks = set()
                for batch in (1, 4, 16):
                    series = sorted((r for r in rows if int(r["batch"]) == batch), key=lambda r: int(r["ranks"]))
                    x = [int(r["ranks"]) for r in series]
                    ranks.update(x)
                    ax.plot(x, [float(r[metric]) for r in series], "o-", color=colors[batch], label=f"B={batch}")
                x = sorted(ranks)
                ax.plot(x, x, "--", color="0.65", label="Ideal")
                ax.set_xscale("log", base=2)
                ax.set_xticks(x)
                ax.set_xticklabels([str(value) for value in x])
                ax.set(xlabel="MPI ranks (log2 scale)", ylabel="Speedup", title=title)
                ax.grid(alpha=.2)
                ax.legend(frameon=False)
            fig.suptitle(f"Q3(a): {name}, one PSC node")
            finish(fig, f"q3a_speedup_{name}")

        rows = sorted((r for r in batches if r["input"] == name), key=lambda r: int(r["batch"]))
        if rows:
            fig, axes = plt.subplots(1, 3, figsize=(12, 4))
            x = [int(r["batch"]) for r in rows]
            for ax, metric, title, unit in zip(axes, ("computation_sec", "total_sec", "cost"),
                                               ("Computation time", "Total program time", "Routing cost"),
                                               ("Seconds", "Seconds", "Cost")):
                center = [float(r[metric + "_median"]) for r in rows]
                low = [m - float(r[metric + "_min"]) for r, m in zip(rows, center)]
                high = [float(r[metric + "_max"]) - m for r, m in zip(rows, center)]
                ax.errorbar(x, center, yerr=[low, high], marker="o", capsize=3, color="#0072B2")
                ax.set_xticks(x)
                ax.tick_params(axis="x", labelsize=8)
                ax.set(xlabel="Batch size", ylabel=unit, title=title)
                ax.grid(alpha=.2)
            fig.suptitle(f"Q3(c): {name}, 32 ranks; median and min/max")
            finish(fig, f"q3c_batches_{name}")

        rows = sorted((r for r in nodes if r["input"] == name), key=lambda r: int(r["ranks_per_node"]))
        if rows:
            fig, axes = plt.subplots(1, 2, figsize=(10, 4))
            x = [int(r["ranks_per_node"]) for r in rows]
            for ax, metric, title in zip(axes, ("computation_sec_median", "total_sec_median"),
                                         ("Computation time", "Total program time")):
                ax.plot(x, [float(r["one_node_" + metric]) for r in rows], "o-", label="1 node: P total ranks")
                ax.plot(x, [float(r["two_node_" + metric]) for r in rows], "s-", label="2 nodes: 2P total ranks")
                ax.set_xscale("log", base=2)
                ax.set_xticks(x)
                ax.set_xticklabels([str(value) for value in x])
                ax.set(xlabel="Ranks per node, P (log2 scale)", ylabel="Seconds", title=title)
                ax.grid(alpha=.2)
                ax.legend(frameon=False)
            fig.suptitle(f"Q3(d): {name}, B=16")
            finish(fig, f"q3d_nodes_{name}")

        for batch in (1, 4, 16):
            rows = sorted((r for r in comparison if r["input"] == name and int(r["batch"]) == batch),
                          key=lambda r: int(r["ranks"]))
            if not rows:
                continue
            fig, axes = plt.subplots(1, 2, figsize=(10, 4))
            x = [int(r["ranks"]) for r in rows]
            for ax, metric, title in zip(axes, ("total_speedup", "computation_speedup"),
                                         ("Total program speedup", "Computation speedup")):
                for platform, marker in (("psc", "o"), ("ghc", "s")):
                    ax.plot(x, [float(r[platform + "_" + metric]) for r in rows],
                            marker=marker, label=platform.upper())
                ax.set_xticks(x)
                ax.set(xlabel="MPI ranks (common range)", ylabel="Speedup", title=title)
                ax.grid(alpha=.2)
                ax.legend(frameon=False)
            fig.suptitle(f"Q3(b): {name}, B={batch}; same source version")
            finish(fig, f"q3b_compare_{name}_b{batch}")
    print(f"Figures: {output} (PNG and PDF)")


if __name__ == "__main__":
    main()
