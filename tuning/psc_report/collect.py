#!/usr/bin/env python3
"""Run one frozen Q3 job inside its matching PSC Slurm allocation."""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess

from common import cases_for, digest, load_campaign, plain_environment, read_run, run_logged, save_json, verify_placement
from common import require_active_allocation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--campaign", type=Path, required=True)
    parser.add_argument("--job", required=True)
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    if args.timeout < 1:
        parser.error("--timeout must be positive")
    campaign, manifest = load_campaign(args.campaign)
    job = next((j for j in manifest["jobs"] if j["name"] == args.job), None)
    if job is None:
        raise ValueError(f"Unknown job: {args.job}")
    if not os.environ.get("SLURM_JOB_ID"):
        raise RuntimeError("Run this inside the allocated PSC Slurm job, not on a login node.")
    nodes = os.environ.get("SLURM_JOB_NUM_NODES", os.environ.get("SLURM_NNODES", ""))
    if nodes != str(job["nodes"]):
        raise RuntimeError(f"This job requires {job['nodes']} allocated node(s).")
    env = plain_environment()
    require_active_allocation()
    for command in ("make", "mpic++", "mpirun", "timeout"):
        if not shutil.which(command):
            raise RuntimeError(f"Required command missing: {command}; load the assignment's MPI/compiler modules.")
    directory = campaign / "jobs" / job["name"]
    directory.mkdir(parents=True, exist_ok=True)
    source = directory / "source"
    work = directory / "work"
    (work / "outputs").mkdir(parents=True, exist_ok=True)
    if not source.exists():
        shutil.copytree(campaign / "source", source)
    for relative, expected in manifest["hashes"].items():
        if relative.startswith("source/") and digest(directory / relative) != expected:
            raise ValueError(f"Job source differs from frozen campaign: {relative}")
    binary = source / "wireroute"
    metadata_path = directory / "job.json"
    if metadata_path.exists():
        metadata = json.loads(metadata_path.read_text())
        if metadata["binary_sha256"] != digest(binary):
            raise ValueError("Saved executable changed; do not mix binaries in a resumed job.")
    else:
        run_logged(["make", "-C", str(source), "wireroute"], directory, directory / "build.log", args.timeout, env)
        metadata = {
            "job": job, "slurm_job_id": os.environ["SLURM_JOB_ID"], "host": socket.gethostname(),
            "nodelist": os.environ.get("SLURM_JOB_NODELIST", ""),
            "binary_sha256": digest(binary), "source_sha256": manifest["source_sha256"],
            "launch_policy": "plain-mpirun",
        }
        save_json(metadata_path, metadata)
        with (directory / "machine.txt").open("w") as log:
            for command in (["lscpu"], ["mpirun", "--version"], ["mpic++", "--version"]):
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, env=env)
    # This must be a shared working path: every node needs the same cwd.
    placement_log = directory / "placement.log"
    total_ranks = job["nodes"] * job["per_node"]
    run_logged(["mpirun", "-np", str(total_ranks), "hostname"], work, placement_log, args.timeout, env)
    metadata["placement"] = verify_placement(placement_log, job["nodes"], job["per_node"])
    if not metadata.get("sessions") or metadata["sessions"][-1]["slurm_job_id"] != os.environ["SLURM_JOB_ID"]:
        metadata.setdefault("sessions", []).append({
            "slurm_job_id": os.environ["SLURM_JOB_ID"], "host": socket.gethostname(),
            "placement": metadata["placement"],
        })
    save_json(metadata_path, metadata)
    print(f"Verified placement: {metadata['placement']}", flush=True)
    cases = cases_for(job, manifest["config"])
    for repeat in range(1, manifest["config"]["repeats"] + 1):
        order = cases if repeat % 2 else list(reversed(cases))
        for name, batch, ranks in order:
            case_dir = directory / "runs" / f"{name}-b{batch}-n{ranks}-r{repeat}"
            result_path = case_dir / "result.json"
            if result_path.exists():
                print(f"Skip completed: {case_dir.name}", flush=True)
                continue
            case_dir.mkdir(parents=True, exist_ok=True)
            require_active_allocation()
            command = ["mpirun", "-np", str(ranks), str(binary), "-f",
                       str(source / "inputs" / f"{name}_wires.txt"), "-b", str(batch), "-i", "5", "-p", "0.1"]
            print(f"Run {job['name']}: {case_dir.name}", flush=True)
            total = run_logged(command, work, case_dir / "program.log", args.timeout, env)
            require_active_allocation()
            result = {
                "job": job["name"], "suite": job["suite"], "nodes": job["nodes"],
                "ranks_per_node": ranks if job["suite"] == "single" else job["per_node"],
                "input": name, "batch": batch, "ranks": ranks, "repeat": repeat,
                "slurm_job_id": os.environ["SLURM_JOB_ID"],
                "total_sec": total, "command": command,
            }
            result.update(read_run(case_dir / "program.log", ranks))
            save_json(result_path, result)
            print(f"  VALID compute={result['computation_sec']:.6f}s total={total:.6f}s cost={result['cost']}", flush=True)
    save_json(directory / "complete.json", {
        "runs": len(cases) * manifest["config"]["repeats"], "source_sha256": manifest["source_sha256"]})
    print(f"Completed {job['name']}. Summarize the campaign after all planned jobs finish.", flush=True)


if __name__ == "__main__":
    main()
