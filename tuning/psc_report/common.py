"""Shared collection utilities. Application launches use only mpirun -np N."""
from collections import Counter
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
INPUTS = ("few", "medium", "abundant")
RANKS = (1, 2, 4, 8, 16, 32, 64, 128)
CURVE_BATCHES = (1, 4, 16)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def save_json(path, value):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def load_campaign(path):
    path = Path(path).resolve()
    manifest = json.loads((path / "campaign.json").read_text())
    if manifest.get("launch_policy") != "plain-mpirun":
        raise ValueError("The campaign must use plain mpirun.")
    for relative, expected in manifest["hashes"].items():
        if digest(path / relative) != expected:
            raise ValueError(f"Frozen campaign file changed: {relative}")
    return path, manifest


def make_jobs(suite):
    jobs = []
    if suite in ("all", "single"):
        jobs.append({"name": "single", "suite": "single", "nodes": 1, "per_node": 128})
    if suite in ("all", "multi"):
        for per_node in (2, 8, 64):
            for nodes in (1, 2):
                jobs.append({"name": f"layout-n{nodes}-p{per_node}", "suite": "layout",
                             "nodes": nodes, "per_node": per_node})
    return jobs


def cases_for(job, config):
    if job["suite"] == "single":
        pairs = [(batch, ranks) for batch in CURVE_BATCHES for ranks in RANKS]
        pairs += [(batch, 32) for batch in config["sensitivity_batches"] if batch not in CURVE_BATCHES]
    else:
        pairs = [(16, job["nodes"] * job["per_node"])]
    return [(name, batch, ranks) for name in config["inputs"] for batch, ranks in pairs]


def prepare_campaign(path, config, jobs):
    path = Path(path).resolve()
    path.mkdir(parents=True, exist_ok=False)
    source = path / "source"
    (source / "inputs").mkdir(parents=True)
    (path / "tools").mkdir()
    (path / "logs").mkdir()
    for name in ("wireroute.cpp", "wireroute.h", "validate.cpp", "Makefile"):
        shutil.copy2(ROOT / "code" / name, source / name)
    for name in config["inputs"]:
        shutil.copy2(ROOT / "code/inputs/timeinput" / f"{name}_wires.txt", source / "inputs")
    for pattern in ("*.py", "*.sh"):
        for tool in HERE.glob(pattern):
            shutil.copy2(tool, path / "tools" / tool.name)
    hashes = {str(p.relative_to(path)): digest(p)
              for directory in (source, path / "tools") for p in directory.rglob("*") if p.is_file()}
    manifest = {
        "schema": 1, "created": datetime.now().isoformat(), "launch_policy": "plain-mpirun",
        "config": config, "jobs": jobs, "hashes": hashes,
        "source_sha256": hashes["source/wireroute.cpp"],
        "total_time_definition": "Wall time from launching mpirun until exit, including MPI startup, validation, output and teardown.",
        "output_filesystem": "Reused work/outputs directory in the shared PSC campaign directory; shared path required on all nodes.",
    }
    save_json(path / "campaign.json", manifest)
    return path, manifest


def plain_environment():
    overrides = [key for key in os.environ if key.startswith(
        ("OMPI_MCA_", "OPAL_MCA_", "PMIX_MCA_", "PRTE_MCA_"))]
    if overrides:
        raise RuntimeError("Unset custom MPI runtime settings: " + ", ".join(sorted(overrides)))
    return dict(os.environ, LC_ALL="C")


def require_active_allocation():
    job_id = os.environ.get('SLURM_JOB_ID')
    if not job_id:
        raise SystemExit('Run inside a PSC Slurm allocation.')
    reply = subprocess.run(['squeue', '--noheader', '--jobs', job_id, '--format=%T'],
                           capture_output=True, text=True, check=True, timeout=15)
    if reply.stdout.split() != ['RUNNING']:
        raise SystemExit('Allocation is no longer RUNNING. Stop and obtain a fresh allocation; '
                         'completed measurements are retained.')


def run_logged(command, cwd, log, timeout, env):
    with log.open("w") as output:
        start = time.perf_counter()
        process = subprocess.Popen(
            ["timeout", "--kill-after=5s", f"{timeout}s", *command], cwd=cwd, env=env,
            stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            status = process.wait()
        except KeyboardInterrupt:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            raise
        elapsed = time.perf_counter() - start
    if status:
        raise RuntimeError(f"Command exited {status}; see {log}")
    return elapsed


def read_run(path, ranks):
    text = path.read_text()
    if "Validate Passed" not in text:
        raise ValueError(f"Routing validation missing: {path}")

    def number(label):
        match = re.search(re.escape(label) + r"\s*([0-9.eE+-]+)", text)
        if not match:
            raise ValueError(f"Missing {label} in {path}")
        return float(match.group(1))

    if int(number("Number of processes:")) != ranks:
        raise ValueError(f"Unexpected process count in {path}")
    initialization = number("Initialization time (sec):")
    computation = number("Computation time (sec):")
    return {
        "initialization_sec": initialization, "computation_sec": computation,
        "init_plus_compute_sec": initialization + computation,
        "cost": int(number("Total cost:")), "max_occupancy": int(number("Max occupancy:")),
    }


def verify_placement(log, nodes, per_node):
    hosts = Counter(line.strip() for line in log.read_text().splitlines() if line.strip())
    if len(hosts) != nodes or any(count != per_node for count in hosts.values()):
        raise ValueError(f"MPI placement {dict(hosts)} does not match {nodes} node(s), "
                         f"{per_node} ranks/node. Check the Slurm task allocation; no placement flags are added.")
    return dict(hosts)
