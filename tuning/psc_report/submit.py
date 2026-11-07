#!/usr/bin/env python3
"""Plan or submit Q3 jobs. Default behavior prints the plan without submitting."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import shlex
import shutil
import subprocess

from common import HERE, INPUTS, cases_for, load_campaign, make_jobs, prepare_campaign, save_json


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--submit", action="store_true", help="Actually queue jobs; omitted means dry run.")
    parser.add_argument("--dry-run", action="store_true", help="Print the plan without creating or submitting anything.")
    parser.add_argument("--prepare-only", action="store_true", help="Freeze a campaign without submitting jobs.")
    parser.add_argument("--suite", choices=("all", "single", "multi", "q3c128"), default="all")
    parser.add_argument("--inputs", nargs="+", choices=INPUTS, default=list(INPUTS))
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--sensitivity-batches", nargs="+", type=int, choices=range(1, 17),
                        default=list(range(1, 17)))
    parser.add_argument("--account", help="Optional Slurm project account.")
    parser.add_argument("--partition", default="RM")
    parser.add_argument("--time", default="01:00:00")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--resume", type=Path, help="Submit only unfinished jobs from a frozen campaign.")
    args = parser.parse_args()
    if sum((args.submit, args.dry_run, args.prepare_only)) > 1:
        parser.error("Choose --submit, --dry-run, or --prepare-only")
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    if args.resume and args.out:
        parser.error("Choose --resume or --out")
    if len(args.inputs) != len(set(args.inputs)) or len(args.sensitivity_batches) != len(set(args.sensitivity_batches)):
        parser.error("Inputs and sensitivity batches must be unique")
    if args.submit and not shutil.which("sbatch"):
        raise RuntimeError("Run submission on PSC, with the assignment's MPI modules loaded.")
    if args.resume:
        campaign, manifest = load_campaign(args.resume)
        config, jobs = manifest["config"], manifest["jobs"]
    else:
        campaign = (args.out or HERE / "results" / f"campaign-{datetime.now():%Y%m%d-%H%M%S}").resolve()
        config = {"inputs": args.inputs, "repeats": args.repeats,
                  "sensitivity_batches": sorted(args.sensitivity_batches), "SA_prob": 0.1, "SA_iters": 5}
        jobs = make_jobs(args.suite)
        if args.submit or args.prepare_only:
            campaign, manifest = prepare_campaign(campaign, config, jobs)
    print(f"Campaign: {campaign}", flush=True)
    print("Application launch policy: plain mpirun -np N; no custom MPI settings.", flush=True)
    submissions = []
    for job in jobs:
        if args.resume and (campaign / "jobs" / job["name"] / "complete.json").exists():
            print(f"Skip completed: {job['name']}", flush=True)
            continue
        count = len(cases_for(job, config)) * config["repeats"]
        command = [
            "sbatch", "--parsable", f"--partition={args.partition}", f"--time={args.time}",
            f"--nodes={job['nodes']}", f"--ntasks-per-node={job['per_node']}",
            f"--ntasks={job['nodes'] * job['per_node']}", "--cpus-per-task=1",
            f"--job-name=wr-q3-{job['name']}", f"--chdir={campaign}",
            f"--output={campaign / 'logs' / (job['name'] + '-%j.log')}",
        ]
        if args.account:
            command += [f"--account={args.account}"]
        command += [str(campaign / "tools/job.sh"), str(campaign), job["name"]]
        print(f"{job['name']}: {count} routing runs; allocation {job['nodes']} node(s), "
              f"{job['per_node']} tasks/node", flush=True)
        print(" ".join(shlex.quote(part) for part in command), flush=True)
        if args.submit:
            print(f"Submitting {job['name']} with sbatch (300-second timeout)...", flush=True)
            try:
                reply = subprocess.check_output(command, universal_newlines=True, timeout=300).strip()
            except subprocess.TimeoutExpired:
                raise SystemExit("sbatch did not respond within 60 seconds. Check squeue and the "
                                 "campaign submissions file before retrying to avoid duplicate jobs.")
            submissions.append({"name": job["name"], "job_id": reply.split(";")[0], "command": command})
            save_json(campaign / f"submissions-{datetime.now():%Y%m%d-%H%M%S}.json", submissions)
            print(f"Submitted {job['name']}: Slurm job {submissions[-1]['job_id']}", flush=True)
    if args.prepare_only:
        print("Campaign prepared without submitting jobs. For an existing allocation, run:", flush=True)
        print(f"python3 {campaign / 'tools/collect.py'} --campaign {shlex.quote(str(campaign))} --job JOB_NAME", flush=True)
    elif not args.submit:
        print("Dry run only. Add --submit on PSC to freeze the source and queue these jobs.")
    else:
        print("After all jobs finish:", flush=True)
        print(" ".join(shlex.quote(part) for part in
                         ("python3", str(campaign / 'tools/summarize.py'), str(campaign), "--plots")), flush=True)


if __name__ == "__main__":
    main()
