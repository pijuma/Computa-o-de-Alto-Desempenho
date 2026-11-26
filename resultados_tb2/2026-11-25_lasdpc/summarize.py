from __future__ import annotations

import argparse
import csv
import statistics
import math
from pathlib import Path

LOADS = ("pequena", "media", "grande")
METRICS = ("tempo_s", "speedup", "eficiencia_pct")


def summarize(source: Path, dest: Path) -> None:
    with source.open() as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if not rows or any(row["status"] != "ok" or row["stage"] != "measure" for row in rows):
        raise ValueError("Campaign contains failures or non-production records")
    configs = list(dict.fromkeys(row["config"] for row in rows))
    if "seq" not in configs:
        raise ValueError("Missing sequential baseline")

    fields = ("carga", "configuracao", "ranks", "threads", "n", "media_s",
              "desvio_s", "ic95_semilargura_s", "speedup", "eficiencia_pct", "checksum")
    stats = {}
    for load in LOADS:
        reference = {row["checksum"] for row in rows if row["load"] == load}
        if len(reference) != 1 or not next(iter(reference)).isdigit():
            raise ValueError(f"Divergent or missing checksum for {load}")
        baseline = None
        for config in ["seq", *(key for key in configs if key != "seq")]:
            group = [row for row in rows if (row["load"], row["config"]) == (load, config)]
            if sorted(int(row["repetition"]) for row in group) != list(range(6)):
                raise ValueError(f"Missing warmup or repeated measurement for {load}/{config}")
            placements = {(int(row["ranks"]), int(row["threads"])) for row in group}
            if len(placements) != 1:
                raise ValueError(f"Inconsistent worker counts for {load}/{config}")
            (ranks, threads), = placements
            times = [float(row["kernel_seconds"]) for row in group if row["repetition"] != "0"]
            if any(not math.isfinite(value) or value <= 0 for value in times):
                raise ValueError(f"Invalid time for {load}/{config}")
            mean = statistics.mean(times)
            sd = statistics.stdev(times)
            if config == "seq":
                baseline = mean
            assert baseline is not None
            speedup = baseline / mean
            stats[load, config] = dict(zip(fields, (
                load, config, ranks, threads, len(times), f"{mean:.6f}", f"{sd:.6f}",
                f"{2.776445 * sd / (len(times) ** .5):.6f}", f"{speedup:.6f}",
                f"{100 * speedup / (ranks * threads):.6f}", next(iter(reference)),
            )))
    with (dest / "summary.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(stats.values())
    with (dest / "desempenho.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["configuracao", *(f"{load}_{metric}" for load in LOADS for metric in METRICS)])
        for config in configs:
            writer.writerow([config, *(stats[load, config][field]
                for load in LOADS for field in ("media_s", "speedup", "eficiencia_pct"))])


    with (dest / "placement.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["carga", "contraste", "media_diferenca_s", "ic95_inferior_s", "ic95_superior_s", "n"])
        for load in LOADS:
            lookup = {(row["config"], row["repetition"]): float(row["kernel_seconds"])
                      for row in rows if row["load"] == load}
            differences = [lookup["two-2x2", str(i)] - lookup["one-2x2", str(i)]
                           for i in range(1, 6)]
            average = statistics.mean(differences)
            half = 2.776445 * statistics.stdev(differences) / (len(differences) ** .5)
            writer.writerow([load, "dois_nos_menos_um_no_2x2",
                             f"{average:.6f}", f"{average-half:.6f}",
                             f"{average+half:.6f}", len(differences)])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("results", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    summarize(args.results, args.output)


if __name__ == "__main__":
    main()
