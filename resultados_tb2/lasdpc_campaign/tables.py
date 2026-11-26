from __future__ import annotations

import argparse
import csv
from pathlib import Path


def table(load: str, rows: list[dict[str, str]]) -> str:
    parts = [
        r"\begin{table}[htbp]", r"\centering",
        r"\caption{Carga "+load+r". Média de cinco medições e semilargura do IC de 95\% do tempo.}",
        r"\label{tab:lasdpc-"+load+"}",
        r"\begin{tabular}{llrrr}", r"\toprule",
        r"Configuração & Nós & Tempo (s) & Speedup & Eficiência (\%) \\",
        r"\midrule",
    ]
    for row in rows:
        name = row["configuracao"]
        label = (r"Sequencial" if name == "seq"
                 else f'${row["ranks"]} \\times {row["threads"]}$')
        nodes = 2 if name.startswith("two-") else 1
        mean = float(row["media_s"])
        half = float(row["ic95_semilargura_s"])
        speedup = float(row["speedup"])
        efficiency = float(row["eficiencia_pct"])
        parts.append(f"{label} & {nodes} & ${mean:.4f} \\pm {half:.4f}$ & {speedup:.2f} & {efficiency:.1f} " + r"\\")
    parts.extend([r"\bottomrule", r"\end{tabular}", r"\end{table}"])
    return "\n".join(parts)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("summary", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with args.summary.open() as stream:
        rows = list(csv.DictReader(stream))
    loads = ("pequena", "media", "grande")
    args.output.write_text("\n\n".join(table(load, [r for r in rows if r["carga"] == load])
                                            for load in loads)+"\n")


if __name__ == "__main__":
    main()
