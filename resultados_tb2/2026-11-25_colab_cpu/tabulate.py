from pathlib import Path
import csv
import json

root = Path("results")
summary = json.loads((root / "summary.json").read_text())
with Path("results.csv").open("w", newline="") as output:
    writer = csv.writer(output)
    writer.writerow(["case", "returncode", "wall_seconds", "kernel_seconds", "checksum", "comparison"])
    for record in summary["records"]:
        name = record["name"]
        if not (name.startswith(("entrada_", "degenerate")) or name == "missing"):
            continue
        lines = (root / "runs" / name / "stdout").read_text().splitlines()
        values = dict(line.split(": ", 1) for line in lines if ": " in line)
        writer.writerow([name, record["returncode"], record["wall_seconds"],
                         values.get("tempo"), values.get("checksum"),
                         "FAIL" if name in summary["failures"] else "PASS"])
print(json.dumps({"records":len(summary["records"]), "failures":summary["failures"]}, indent=2))
