from pathlib import Path
import hashlib
import json
import os
import resource
import shlex
import subprocess
import time

root = Path("/content/tb2-audit")
root.mkdir(exist_ok=False)
os.chdir(root)
subprocess.run(["tar", "-xzf", "/content/tb2-source.tgz"], check=True)
src = root / "source"
logs = root / "runs"
logs.mkdir()
hosts = root / "hosts"
hosts.write_text("localhost slots=2\n")
env = dict(os.environ, OMP_DYNAMIC="FALSE", OMP_SCHEDULE="static", OMP_PROC_BIND="close", OMP_PLACES="cores")
records = []
failures = []

def run(name: str, command: list[str]) -> dict:
    path = logs / name
    path.mkdir()
    start = time.monotonic()
    code = 124
    with (path / "stdout").open("w") as out, (path / "stderr").open("w") as err:
        try:
            code = subprocess.run(command, cwd=src, env=env, stdout=out, stderr=err, timeout=100).returncode
        except subprocess.TimeoutExpired:
            err.write("\nTIMEOUT\n")
    result = dict(name=name, command=command, returncode=code, wall_seconds=time.monotonic()-start)
    (path / "run.json").write_text(json.dumps(result, indent=2))
    records.append(result)
    return result

def lines(name: str) -> list[str]:
    return (logs / name / "stdout").read_text().splitlines()

def mpi(ranks: int, threads: int, entry: Path) -> list[str]:
    return ["mpirun", "--allow-run-as-root", "--hostfile", str(hosts),
            "--map-by", f"slot:PE={threads}", "--bind-to", "core", "--report-bindings",
            "-np", str(ranks), "./fire_mpi_omp", str(entry)]

def archive() -> None:
    (root / "summary.json").write_text(json.dumps(dict(records=records, failures=failures), indent=2))
    files = sorted(p for p in root.rglob("*") if p.is_file() and p.name != "SHA256SUMS")
    (root / "SHA256SUMS").write_text("".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(root)}\n" for p in files))
    subprocess.run(["tar", "-czf", "/content/tb2-colab-results.tgz", "-C", str(root), "."], check=True)

try:
    (root / "environment.json").write_text(json.dumps(dict(
        host=os.uname().nodename, kernel=os.uname().release, affinity=sorted(os.sched_getaffinity(0)),
        omp={key:env[key] for key in ("OMP_DYNAMIC","OMP_SCHEDULE","OMP_PROC_BIND","OMP_PLACES")},
        cpu_limit=[Path(p).read_text() for p in ("/sys/fs/cgroup/cpu.max","/sys/fs/cgroup/memory.max") if Path(p).exists()],
        gpu_device_files=[str(p) for p in Path("/dev").glob("nvidia*")],
        source_date_utc=time.strftime("%FT%TZ", time.gmtime()),
    ), indent=2))
    for name, cmd in {
        "cpu":["lscpu"], "memory":["free","-b"], "gcc":["gcc","--version"],
        "mpi":["mpirun","--version"], "mpicc":["mpicc","--showme"], "libc":["ldd","--version"],
        "build":["make","CC=gcc","CFLAGS=-Wall -Wextra -Werror -O3 -fopenmp","fire_seq","fire_mpi_omp"],
    }.items():
        if run(name, cmd)["returncode"]:
            raise RuntimeError(name)
    cases = sorted((src / "data").glob("*.txt"))
    edge = src / "degenerate.txt"
    edge.write_text("1 7 0 2 13 100\n0 1 0\n0 0\n")
    cases.append(edge)
    for entry in cases:
        fields = entry.read_text().splitlines()
        for threads in (1,2):
            header = fields[0].split()
            header[3] = str(threads)
            (src / f"{entry.stem}-t{threads}.txt").write_text("\n".join([" ".join(header), *fields[1:]])+"\n")
        base = f"{entry.stem}-seq"
        t1 = src / f"{entry.stem}-t1.txt"
        if run(base, ["./fire_seq", str(t1)])["returncode"] or len(lines(base)) != 12:
            failures.append(base)
        for ranks, threads in ((1,1),(1,2),(2,1)):
            name = f"{entry.stem}-{ranks}x{threads}"
            inp = src / f"{entry.stem}-t{threads}.txt"
            code = run(name, mpi(ranks, threads, inp))["returncode"]
            if code or len(lines(name)) != 12 or lines(name)[:11] != lines(base)[:11]:
                failures.append(name)
    code = run("missing", mpi(2, 1, src / "missing.txt"))["returncode"]
    if code in (0,124) or lines("missing"):
        failures.append("missing")
except Exception as exc:
    failures.append(repr(exc))
finally:
    archive()
print(json.dumps(dict(records=len(records), failures=failures), indent=2))
