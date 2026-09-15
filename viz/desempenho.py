#!/usr/bin/env python3

import argparse
import csv
import os
import re
import statistics
import subprocess
import tempfile
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt

BASE_DIR = Path(__file__).resolve().parent
RAIZ = BASE_DIR.parent
ENTRADAS_PADRAO = {
    "Pequena": RAIZ / "data" / "entrada_carga_pequena.txt",
    "Média": RAIZ / "data" / "entrada_carga_media.txt",
    "Grande": RAIZ / "data" / "entrada_carga_grande.txt",
}
CORES = ["#4C78A8", "#F28E2B", "#59A14F"]
SCHEDULES = [
    ("static", "static"),
    ("static-16", "static,16"),
    ("dynamic-16", "dynamic,16"),
    ("dynamic-64", "dynamic,64"),
    ("guided-16", "guided,16"),
]


def preparar_entrada(caminho, threads, destino):
    linhas = caminho.read_text(encoding="utf-8").splitlines()
    primeira = linhas[0].split()
    primeira[3] = str(threads)
    linhas[0] = " ".join(primeira)
    destino.write_text("\n".join(linhas) + "\n", encoding="utf-8")


def executar(executavel, entrada):
    ambiente = os.environ.copy()
    ambiente["OMP_DYNAMIC"] = "FALSE"
    processo = subprocess.run(
        [str(executavel), str(entrada)],
        check=True,
        capture_output=True,
        text=True,
        env=ambiente,
    )
    valores = {}
    for linha in processo.stdout.splitlines():
        chave, valor = linha.split(":", 1)
        valores[chave] = valor.strip()
    return float(valores["tempo"]), valores


def validar_resultado(referencia, resultado):
    esperado = {chave: valor for chave, valor in referencia.items() if chave != "tempo"}
    obtido = {chave: valor for chave, valor in resultado.items() if chave != "tempo"}
    if esperado != obtido:
        raise RuntimeError("A versão OpenMP produziu resultado diferente da sequencial")


def compilar(executavel, fonte):
    subprocess.run(
        ["gcc", "-Wall", "-Wextra", "-O2", "-fopenmp", str(fonte), "-o", str(executavel)],
        check=True,
    )


def fonte_com_schedule(schedule):
    codigo = (RAIZ / "fire_omp.c").read_text(encoding="utf-8")
    codigo, atualizacoes = re.subn(
        r"schedule\(static\)", f"schedule({schedule})", codigo
    )
    codigo, ativacoes = re.subn(
        r"([ \t]*)#pragma omp for collapse\(2\)\n",
        rf"\1#pragma omp for collapse(2) schedule({schedule})\n",
        codigo,
        count=1,
    )
    if ativacoes != 1 or atualizacoes != 2:
        raise RuntimeError("Não foi possível aplicar o schedule à versão OpenMP")
    return codigo


def configurar_grafico(eixo, eixo_x, eixo_y):
    eixo.set_xlabel(eixo_x)
    eixo.set_ylabel(eixo_y)
    eixo.xaxis.label.set_fontweight("bold")
    eixo.yaxis.label.set_fontweight("bold")
    eixo.xaxis.labelpad = 10
    eixo.yaxis.labelpad = 10
    eixo.set_facecolor("#F2F2F2")
    eixo.set_axisbelow(True)
    eixo.grid(True, color="white", linewidth=1)
    eixo.spines["top"].set_visible(False)
    eixo.spines["right"].set_visible(False)


def salvar_linhas(dados, threads, destino, metrica, eixo_y, ideal=False):
    figura, eixo = plt.subplots(figsize=(7.2, 4.4), constrained_layout=True)
    for (carga, valores), cor in zip(dados.items(), CORES):
        eixo.plot(
            threads,
            valores[metrica],
            marker="o",
            linewidth=1.8,
            markersize=5,
            color=cor,
            label=carga,
        )
    if ideal:
        eixo.plot(threads, threads, "--", color="#333333", linewidth=1.2, label="Ideal")
    eixo.set_xticks(threads)
    eixo.legend(frameon=False)
    configurar_grafico(eixo, "Número de threads (T)", eixo_y)
    figura.savefig(destino, bbox_inches="tight")
    plt.close(figura)


def salvar_schedules(nomes, tempos, destino):
    figura, eixo = plt.subplots(figsize=(7.2, 4.4), constrained_layout=True)
    cores = ["#4C78A8", "#F28E2B", "#59A14F", "#E15759", "#8E6C9E"]
    barras = eixo.bar(nomes, tempos, color=cores, width=0.68, zorder=3)
    eixo.bar_label(barras, fmt="%.3f", padding=3)
    eixo.tick_params(axis="x", rotation=18)
    eixo.set_ylim(0, max(tempos) * 1.14)
    configurar_grafico(eixo, "Schedule", "Tempo médio (s)")
    figura.savefig(destino, bbox_inches="tight")
    plt.close(figura)


def main():
    parser = argparse.ArgumentParser(description="Mede e gera gráficos de desempenho OpenMP.")
    parser.add_argument("--threads", nargs="+", type=int, default=[1, 2, 4, 8, 16, 32])
    parser.add_argument("--repeticoes", type=int, default=3)
    parser.add_argument("--threads-schedule", type=int, default=8)
    parser.add_argument("--saida", type=Path, default=BASE_DIR / "saida" / "desempenho")
    argumentos = parser.parse_args()

    if argumentos.repeticoes <= 0 or any(valor <= 0 for valor in argumentos.threads):
        parser.error("threads e repeticoes devem ser positivos")

    argumentos.saida.mkdir(parents=True, exist_ok=True)
    compilar(RAIZ / "fire_seq", RAIZ / "fire_seq.c")
    compilar(RAIZ / "fire_omp", RAIZ / "fire_omp.c")

    medicoes = []
    resultados = {}
    with tempfile.TemporaryDirectory(prefix="fire-benchmark-") as temporario:
        temporario = Path(temporario)

        for carga, entrada_original in ENTRADAS_PADRAO.items():
            entrada_seq = temporario / f"{carga}-seq.txt"
            preparar_entrada(entrada_original, 1, entrada_seq)
            tempos_seq = []
            referencia = None
            for repeticao in range(1, argumentos.repeticoes + 1):
                tempo, saida = executar(RAIZ / "fire_seq", entrada_seq)
                referencia = saida
                tempos_seq.append(tempo)
                medicoes.append([carga, "sequencial", 1, "-", repeticao, tempo])

            resultados[carga] = {"tempo_seq": statistics.fmean(tempos_seq), "tempo": [], "speedup": [], "eficiencia": []}
            for threads in argumentos.threads:
                entrada_omp = temporario / f"{carga}-omp-{threads}.txt"
                preparar_entrada(entrada_original, threads, entrada_omp)
                tempos_omp = []
                for repeticao in range(1, argumentos.repeticoes + 1):
                    tempo, saida = executar(RAIZ / "fire_omp", entrada_omp)
                    validar_resultado(referencia, saida)
                    tempos_omp.append(tempo)
                    medicoes.append([carga, "omp", threads, "static", repeticao, tempo])
                media = statistics.fmean(tempos_omp)
                speedup = resultados[carga]["tempo_seq"] / media
                resultados[carga]["tempo"].append(media)
                resultados[carga]["speedup"].append(speedup)
                resultados[carga]["eficiencia"].append(speedup / threads)

        entrada_schedule = temporario / "media-schedules.txt"
        preparar_entrada(
            ENTRADAS_PADRAO["Média"], argumentos.threads_schedule, entrada_schedule
        )
        _, referencia_schedule = executar(RAIZ / "fire_seq", entrada_schedule)
        nomes_schedule = []
        tempos_schedule = []
        for nome, schedule in SCHEDULES:
            fonte = temporario / f"fire_omp_{nome}.c"
            executavel = temporario / f"fire_omp_{nome}"
            fonte.write_text(fonte_com_schedule(schedule), encoding="utf-8")
            compilar(executavel, fonte)
            repeticoes = []
            for repeticao in range(1, argumentos.repeticoes + 1):
                tempo, saida = executar(executavel, entrada_schedule)
                validar_resultado(referencia_schedule, saida)
                repeticoes.append(tempo)
                medicoes.append([
                    "Média", "omp-schedule", argumentos.threads_schedule,
                    nome, repeticao, tempo,
                ])
            nomes_schedule.append(nome)
            tempos_schedule.append(statistics.fmean(repeticoes))

    with (argumentos.saida / "medicoes.csv").open("w", newline="", encoding="utf-8") as arquivo:
        escritor = csv.writer(arquivo)
        escritor.writerow(["carga", "versao", "threads", "schedule", "repeticao", "tempo_s"])
        escritor.writerows(medicoes)

    salvar_linhas(
        resultados, argumentos.threads, argumentos.saida / "01_tempo_threads.pdf",
        "tempo", "Tempo médio (s)",
    )
    salvar_linhas(
        resultados, argumentos.threads, argumentos.saida / "02_speedup_threads.pdf",
        "speedup", "Speedup (x)", ideal=True,
    )
    salvar_linhas(
        resultados, argumentos.threads, argumentos.saida / "03_eficiencia_threads.pdf",
        "eficiencia", "Eficiência",
    )
    salvar_schedules(
        nomes_schedule, tempos_schedule,
        argumentos.saida / "04_comparacao_schedules.pdf",
    )
    print(f"Gráficos e medições salvos em: {argumentos.saida.resolve()}")


if __name__ == "__main__":
    main()
