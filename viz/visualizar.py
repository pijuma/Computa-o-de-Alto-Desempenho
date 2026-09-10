#!/usr/bin/env python3

import argparse
import csv
import ctypes
import json
import subprocess
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import BoundaryNorm, ListedColormap
from matplotlib.lines import Line2D
from matplotlib.patches import Patch
from matplotlib.ticker import ScalarFormatter

BASE_DIR = Path(__file__).resolve().parent
LIB_PATH = BASE_DIR / ".build" / "librand_helper.so"

NOMES_ESTADOS = [
    "Não combustível",
    "Intacta",
    "Em chamas",
    "Queimada",
    "Contenção",
]

CORES_ESTADOS = ["#9CA3AF", "#3A923A", "#F59E0B", "#303030", "#2878B5"]

DIRECOES_VENTO = {
    (-1, 0): "norte",
    (-1, 1): "nordeste",
    (0, 1): "leste",
    (1, 1): "sudeste",
    (1, 0): "sul",
    (1, -1): "sudoeste",
    (0, -1): "oeste",
    (-1, -1): "noroeste",
}


def carregar_biblioteca():
    if not LIB_PATH.exists():
        subprocess.run(["make", "-C", str(BASE_DIR)], check=True)

    biblioteca = ctypes.CDLL(str(LIB_PATH))
    ponteiro_int32 = np.ctypeslib.ndpointer(
        dtype=np.int32, ndim=1, flags="C_CONTIGUOUS"
    )
    ponteiro_int8 = np.ctypeslib.ndpointer(dtype=np.int8, ndim=1, flags="C_CONTIGUOUS")

    biblioteca.gerar_floresta.argtypes = [
        ponteiro_int32,
        ponteiro_int32,
        ctypes.c_size_t,
        ctypes.c_uint,
    ]
    biblioteca.gerar_floresta.restype = None
    biblioteca.calcular_checksum.argtypes = [
        ponteiro_int8,
        ponteiro_int8,
        ctypes.c_size_t,
    ]
    biblioteca.calcular_checksum.restype = ctypes.c_uint64
    return biblioteca


def ler_entrada(caminho):
    try:
        valores = [int(valor) for valor in caminho.read_text(encoding="utf-8").split()]
    except (OSError, ValueError) as erro:
        raise ValueError(f"Não foi possível ler a entrada: {erro}") from erro

    posicao = 0

    def retirar(quantidade, descricao):
        nonlocal posicao
        if posicao + quantidade > len(valores):
            raise ValueError(f"Entrada incompleta em: {descricao}")
        resultado = valores[posicao : posicao + quantidade]
        posicao += quantidade
        return resultado

    L, C, P, T, seed, limiar = retirar(6, "configuração geral")
    vento_linha, vento_coluna, intensidade = retirar(3, "configuração do vento")
    F, Z = retirar(2, "quantidade de focos e zonas")

    if L <= 0 or C <= 0 or P < 0 or T <= 0 or limiar <= 0:
        raise ValueError("Configuração geral inválida")
    if vento_linha not in (-1, 0, 1) or vento_coluna not in (-1, 0, 1):
        raise ValueError("Componentes do vento inválidas")
    if (vento_linha, vento_coluna) == (0, 0) or not 0 <= intensidade <= 5:
        raise ValueError("Configuração do vento inválida")
    if F < 0 or Z < 0:
        raise ValueError("Quantidade de focos ou zonas inválida")

    focos = [tuple(retirar(2, f"foco {i}")) for i in range(F)]
    zonas = [tuple(retirar(5, f"zona {i}")) for i in range(Z)]

    if posicao != len(valores):
        raise ValueError("Há valores adicionais após a última zona")

    if len(set(focos)) != len(focos):
        raise ValueError("Existem focos repetidos")
    if any(not (0 <= linha < L and 0 <= coluna < C) for linha, coluna in focos):
        raise ValueError("Existe foco fora da matriz")

    for passo, li, ci, lf, cf in zonas:
        if not (0 <= passo < P and 0 <= li <= lf < L and 0 <= ci <= cf < C):
            raise ValueError("Existe zona de contenção inválida")

    return {
        "L": L,
        "C": C,
        "P": P,
        "T": T,
        "seed": seed,
        "limiar": limiar,
        "vento_linha": vento_linha,
        "vento_coluna": vento_coluna,
        "intensidade": intensidade,
        "focos": focos,
        "zonas": zonas,
    }


def contar_estados(estado):
    contagem = np.bincount(estado.ravel(), minlength=5)
    return [int(valor) for valor in contagem[:5]]


def simular(configuracao, biblioteca):
    L = configuracao["L"]
    C = configuracao["C"]
    tamanho = L * C

    cobertura = np.empty(tamanho, dtype=np.int32)
    umidade = np.empty(tamanho, dtype=np.int32)
    biblioteca.gerar_floresta(
        cobertura,
        umidade,
        tamanho,
        ctypes.c_uint(configuracao["seed"]),
    )
    cobertura = cobertura.reshape(L, C)
    umidade = umidade.reshape(L, C)

    ativacao = np.full((L, C), -1, dtype=np.int32)
    for passo, li, ci, lf, cf in configuracao["zonas"]:
        regiao = ativacao[li : lf + 1, ci : cf + 1]
        sem_ativacao = regiao == -1
        regiao[sem_ativacao] = passo
        np.minimum(regiao, passo, out=regiao)

    estado = np.where(cobertura <= 1, 0, 1).astype(np.int8)
    tempo = np.zeros((L, C), dtype=np.int8)

    for linha, coluna in configuracao["focos"]:
        if cobertura[linha, coluna] <= 1:
            raise ValueError(f"Foco ({linha}, {coluna}) está em célula não combustível")
        estado[linha, coluna] = 2
        tempo[linha, coluna] = 2 if cobertura[linha, coluna] == 2 else 4

    combustiveis_iniciais = int(np.count_nonzero(cobertura >= 2))
    estado_inicial = estado.copy()
    passos_snapshot = [
        int(passo)
        for passo in np.rint(np.linspace(0, max(0, configuracao["P"] - 1), 4))
    ]
    snapshots = {}
    historico = [
        {
            "passo": -1,
            "novas_ignicoes": 0,
            "contencoes_ativadas": 0,
            "estados": contar_estados(estado),
        }
    ]

    pico_passo = -1
    pico_quantidade = 0
    total_ignicoes = 0

    for passo in range(configuracao["P"]):
        if not np.any(estado == 2):
            break

        novas_contencoes = (ativacao == passo) & (estado == 1)
        contencoes_ativadas = int(np.count_nonzero(novas_contencoes))
        estado[novas_contencoes] = 4

        proximo_estado = estado.copy()
        proximo_tempo = tempo.copy()

        em_chamas = estado == 2
        proximo_tempo[em_chamas] = tempo[em_chamas] - 1
        terminou_queima = em_chamas & (proximo_tempo == 0)
        proximo_estado[terminou_queima] = 3

        soma_vizinhos = np.zeros((L, C), dtype=np.int16)
        for delta_linha in (-1, 0, 1):
            for delta_coluna in (-1, 0, 1):
                if delta_linha == 0 and delta_coluna == 0:
                    continue

                linha_inicio = max(0, -delta_linha)
                linha_fim = min(L, L - delta_linha)
                coluna_inicio = max(0, -delta_coluna)
                coluna_fim = min(C, C - delta_coluna)

                viz_linha_inicio = linha_inicio + delta_linha
                viz_linha_fim = linha_fim + delta_linha
                viz_coluna_inicio = coluna_inicio + delta_coluna
                viz_coluna_fim = coluna_fim + delta_coluna

                prop_linha = -delta_linha
                prop_coluna = -delta_coluna
                peso_basico = 10 if abs(prop_linha) + abs(prop_coluna) == 1 else 7
                alinhamento = (
                    prop_linha * configuracao["vento_linha"]
                    + prop_coluna * configuracao["vento_coluna"]
                )
                peso = max(1, peso_basico + configuracao["intensidade"] * alinhamento)

                vizinhos_em_chamas = (
                    estado[
                        viz_linha_inicio:viz_linha_fim,
                        viz_coluna_inicio:viz_coluna_fim,
                    ]
                    == 2
                )
                soma_vizinhos[
                    linha_inicio:linha_fim,
                    coluna_inicio:coluna_fim,
                ] += (
                    vizinhos_em_chamas * peso
                )

        fator = np.where(cobertura == 2, 8, np.where(cobertura == 3, 12, 0)).astype(
            np.int32
        )
        potencial = (soma_vizinhos.astype(np.int32) * fator * (100 - umidade)) // 100

        intactas = estado == 1
        novas_chamas = intactas & (potencial >= configuracao["limiar"])
        proximo_estado[novas_chamas] = 2
        proximo_tempo[novas_chamas & (cobertura == 2)] = 2
        proximo_tempo[novas_chamas & (cobertura == 3)] = 4

        novas_ignicoes = int(np.count_nonzero(novas_chamas))
        total_ignicoes += novas_ignicoes
        estado, tempo = proximo_estado, proximo_tempo

        if novas_ignicoes > pico_quantidade:
            pico_passo = passo
            pico_quantidade = novas_ignicoes

        if passo in passos_snapshot:
            snapshots[passo] = estado.copy()

        historico.append(
            {
                "passo": passo,
                "novas_ignicoes": novas_ignicoes,
                "contencoes_ativadas": contencoes_ativadas,
                "estados": contar_estados(estado),
            }
        )

        if not np.any(estado == 2):
            break

    estado_linear = np.ascontiguousarray(estado.ravel())
    tempo_linear = np.ascontiguousarray(tempo.ravel())
    checksum = int(biblioteca.calcular_checksum(estado_linear, tempo_linear, tamanho))
    contagem_final = contar_estados(estado)

    percentual_queimado = 0.0
    percentual_protegido = 0.0
    if combustiveis_iniciais:
        percentual_queimado = (
            100.0 * (contagem_final[3] + contagem_final[2]) / combustiveis_iniciais
        )
        percentual_protegido = 100.0 * contagem_final[4] / combustiveis_iniciais

    resumo = {
        "passos": len(historico) - 1,
        "nao_combustiveis": contagem_final[0],
        "intactas": contagem_final[1],
        "em_chamas": contagem_final[2],
        "queimadas": contagem_final[3],
        "contencao": contagem_final[4],
        "total_ignicoes": total_ignicoes,
        "pico_passo": pico_passo,
        "pico_quantidade": pico_quantidade,
        "percentual_queimado": percentual_queimado,
        "percentual_protegido": percentual_protegido,
        "checksum": checksum,
    }

    ultimo_passo = historico[-1]["passo"]
    snapshots_ordenados = []
    for passo in passos_snapshot:
        if passo in snapshots:
            snapshots_ordenados.append((passo, snapshots[passo]))
        elif ultimo_passo >= 0:
            snapshots_ordenados.append((ultimo_passo, estado.copy()))
        else:
            snapshots_ordenados.append((-1, estado_inicial.copy()))

    return {
        "historico": historico,
        "estado_final": estado,
        "focos": configuracao["focos"],
        "snapshots": snapshots_ordenados,
        "resumo": resumo,
    }


def salvar_figura(figura, destino, nome):
    figura.savefig(destino / f"{nome}.pdf", bbox_inches="tight")
    plt.close(figura)


def aplicar_estilo_eixos(eixo):
    eixo.grid(False)
    eixo.spines["top"].set_visible(False)
    eixo.spines["right"].set_visible(False)
    eixo.spines["left"].set_linewidth(1.2)
    eixo.spines["bottom"].set_linewidth(1.2)
    eixo.tick_params(direction="out", top=False, right=False, width=1.2)
    eixo.xaxis.label.set_fontweight("bold")
    eixo.yaxis.label.set_fontweight("bold")
    eixo.xaxis.labelpad = 10
    eixo.yaxis.labelpad = 10


def grafico_snapshots(resultado, destino):
    snapshots = resultado["snapshots"]
    linhas, colunas = snapshots[0][1].shape
    proporcao_matriz = colunas / linhas
    largura_figura = 18
    margem_esquerda = 0.045
    margem_direita = 0.99
    espacamento_horizontal = 0.12
    margem_superior = 0.38
    margem_inferior = 1.05
    largura_util = largura_figura * (margem_direita - margem_esquerda)
    largura_eixo = largura_util / (4 + 3 * espacamento_horizontal)
    altura_mapa = largura_eixo / proporcao_matriz
    altura_figura = margem_superior + altura_mapa + margem_inferior
    cmap = ListedColormap(CORES_ESTADOS)
    norm = BoundaryNorm(np.arange(-0.5, 5.5, 1), cmap.N)
    figura, eixos = plt.subplots(
        1, 4, figsize=(largura_figura, altura_figura), constrained_layout=False
    )
    figura.subplots_adjust(
        left=margem_esquerda,
        right=margem_direita,
        top=1 - margem_superior / altura_figura,
        bottom=margem_inferior / altura_figura,
        wspace=espacamento_horizontal,
    )

    for indice, (eixo, (passo, mapa)) in enumerate(zip(eixos, snapshots)):
        eixo.imshow(mapa, cmap=cmap, norm=norm, interpolation="nearest", aspect="equal")
        if indice == 0 and resultado["focos"]:
            linhas_focos, colunas_focos = zip(*resultado["focos"])
            eixo.scatter(
                colunas_focos,
                linhas_focos,
                marker="*",
                s=115,
                color="#D7191C",
                edgecolors="white",
                linewidths=0.7,
                zorder=5,
            )
        eixo.text(
            0.5,
            1.04,
            f"Passo {passo}" if passo >= 0 else "Antes do passo 0",
            transform=eixo.transAxes,
            ha="center",
            va="bottom",
            fontweight="bold",
        )
        formatador_coluna = ScalarFormatter(useMathText=True)
        formatador_coluna.set_powerlimits((0, 0))
        formatador_linha = ScalarFormatter(useMathText=True)
        formatador_linha.set_powerlimits((0, 0))
        eixo.xaxis.set_major_formatter(formatador_coluna)
        eixo.yaxis.set_major_formatter(formatador_linha)
        aplicar_estilo_eixos(eixo)
        if indice == 0:
            eixo.set_xlabel("Coluna")
            eixo.set_ylabel("Linha")
        else:
            eixo.tick_params(
                left=False,
                bottom=False,
                labelleft=False,
                labelbottom=False,
            )

    legenda = [
        Patch(facecolor=cor, label=nome)
        for cor, nome in zip(CORES_ESTADOS, NOMES_ESTADOS)
    ]
    legenda.append(
        Line2D(
            [0],
            [0],
            marker="*",
            color="none",
            markerfacecolor="#D7191C",
            markeredgecolor="#8F2419",
            markersize=11,
            label="Foco inicial",
        )
    )
    figura.legend(
        handles=legenda,
        loc="lower right",
        bbox_to_anchor=(0.99, 0.16 / altura_figura),
        ncols=6,
        frameon=False,
        handleheight=0.5,
        handlelength=1.6,
        fontsize=11,
    )
    salvar_figura(figura, destino, "01_snapshots")


def grafico_evolucao(resultado, destino):
    historico = resultado["historico"]
    passos = np.array([item["passo"] for item in historico])
    valores = np.array([item["estados"] for item in historico]).T
    figura, eixo = plt.subplots(figsize=(10, 4.7), constrained_layout=True)

    if len(passos) == 1:
        base = 0
        for indice, nome in enumerate(NOMES_ESTADOS):
            eixo.bar(
                [0],
                [valores[indice, 0]],
                bottom=base,
                color=CORES_ESTADOS[indice],
                label=nome,
            )
            base += valores[indice, 0]
    else:
        eixo.stackplot(passos, valores, labels=NOMES_ESTADOS, colors=CORES_ESTADOS)

    eixo.set_xlabel("Passo")
    eixo.set_ylabel("Quantidade de células")

    total_celulas = int(valores[:, 0].sum())
    intervalo_bruto = max(1, total_celulas / 5)
    potencia = 10 ** np.floor(np.log10(intervalo_bruto))
    proporcao = intervalo_bruto / potencia
    multiplicador = (
        1 if proporcao <= 1 else 2 if proporcao <= 2 else 5 if proporcao <= 5 else 10
    )
    intervalo = int(multiplicador * potencia)
    limite = total_celulas * 1.01

    eixo.set_ylim(0, limite)
    eixo.set_yticks(np.arange(0, total_celulas + 1, intervalo))
    formatador = ScalarFormatter(useMathText=True)
    formatador.set_powerlimits((0, 0))
    eixo.yaxis.set_major_formatter(formatador)
    eixo.legend(
        loc="upper right",
        bbox_to_anchor=(1, -0.16),
        ncols=5,
        frameon=False,
        handleheight=0.5,
        handlelength=1.6,
    )
    aplicar_estilo_eixos(eixo)
    eixo.set_facecolor("#F2F2F2")
    eixo.set_axisbelow(True)
    eixo.grid(True, color="white", linewidth=1)
    salvar_figura(figura, destino, "02_evolucao_estados")


def grafico_ignicoes(resultado, destino):
    historico = resultado["historico"][1:]
    passos = [item["passo"] for item in historico] or [0]
    ignicoes = [item["novas_ignicoes"] for item in historico] or [0]
    ativacoes = [item["contencoes_ativadas"] for item in historico] or [0]
    resumo = resultado["resumo"]

    figura, eixo = plt.subplots(figsize=(10, 5), constrained_layout=True)
    eixo.plot(
        passos,
        ignicoes,
        color="#C43C2B",
        linewidth=1.4,
        marker="o",
        markersize=3.5,
        markerfacecolor="#C43C2B",
        markeredgecolor="#8F2419",
        markeredgewidth=0.5,
        label="Novas ignições",
        zorder=3,
    )

    primeira_contencao = True
    for passo, quantidade in zip(passos, ativacoes):
        if quantidade:
            eixo.axvline(
                passo,
                color="#2878B5",
                linestyle="--",
                linewidth=1.2,
                alpha=0.9,
                zorder=2,
                label="Ativação de contenção" if primeira_contencao else None,
            )
            primeira_contencao = False

    if resumo["pico_passo"] >= 0:
        pico_passo = resumo["pico_passo"]
        pico_quantidade = resumo["pico_quantidade"]
        deslocamento = (-20, -18) if pico_passo >= max(passos) / 2 else (20, -18)
        alinhamento = "right" if deslocamento[0] < 0 else "left"

        eixo.vlines(
            pico_passo,
            0,
            pico_quantidade,
            color="#9CA3AF",
            linewidth=1,
            zorder=1.5,
        )
        eixo.annotate(
            f"Pico de ignições:\nPasso = {pico_passo}\nIgnições = {pico_quantidade}",
            xy=(pico_passo, pico_quantidade),
            xytext=deslocamento,
            textcoords="offset points",
            ha=alinhamento,
            va="top",
            bbox={
                "boxstyle": "square,pad=0.45",
                "facecolor": "white",
                "edgecolor": "#6B7280",
                "linewidth": 0.8,
            },
        )

    eixo.set_xlabel("Passo")
    eixo.set_ylabel("Novas ignições")
    eixo.set_ylim(0, max(1, max(ignicoes)) * 1.08)
    formatador = ScalarFormatter(useMathText=True)
    formatador.set_powerlimits((0, 0))
    eixo.yaxis.set_major_formatter(formatador)
    eixo.margins(x=0.02)
    aplicar_estilo_eixos(eixo)
    eixo.set_facecolor("#F2F2F2")
    eixo.set_axisbelow(True)
    eixo.grid(True, color="white", linewidth=1)
    eixo.legend(
        loc="upper right",
        bbox_to_anchor=(1, -0.14),
        ncols=2,
        frameon=False,
        handlelength=1.6,
    )
    salvar_figura(figura, destino, "03_ignicoes_por_passo")


def grafico_regra(configuracao, destino):
    componentes_vento = (
        configuracao["vento_linha"],
        configuracao["vento_coluna"],
    )
    direcao_vento = DIRECOES_VENTO[componentes_vento]
    figura, eixo = plt.subplots(figsize=(7.5, 7), constrained_layout=True)
    eixo.set_xlim(-0.5, 2.5)
    eixo.set_ylim(2.82, -0.5)
    eixo.set_aspect("equal")

    for linha in range(3):
        for coluna in range(3):
            if linha == 1 and coluna == 1:
                texto = "Célula\navaliada"
                cor = "#FDE68A"
            else:
                delta_linha = linha - 1
                delta_coluna = coluna - 1
                prop_linha = -delta_linha
                prop_coluna = -delta_coluna
                basico = 10 if abs(prop_linha) + abs(prop_coluna) == 1 else 7
                alinhamento = (
                    prop_linha * configuracao["vento_linha"]
                    + prop_coluna * configuracao["vento_coluna"]
                )
                peso = max(1, basico + configuracao["intensidade"] * alinhamento)
                texto = f"Pb={basico}\nA={alinhamento}\nPv={peso}"
                cor = "#FED7AA"

            retangulo = plt.Rectangle(
                (coluna - 0.45, linha - 0.45),
                0.9,
                0.9,
                facecolor=cor,
                edgecolor="#4B5563",
                linewidth=1.2,
            )
            eixo.add_patch(retangulo)
            eixo.text(coluna, linha, texto, ha="center", va="center")

    eixo.arrow(
        1,
        1,
        configuracao["vento_coluna"] * 0.32,
        configuracao["vento_linha"] * 0.32,
        width=0.025,
        head_width=0.13,
        color="#2878B5",
        length_includes_head=True,
    )
    eixo.text(
        1,
        2.63,
        f"Vento: {direcao_vento} "
        f"({configuracao['vento_linha']}, {configuracao['vento_coluna']}), "
        f"intensidade = {configuracao['intensidade']}",
        ha="center",
        color="#1F4E79",
    )
    eixo.set_xticks([])
    eixo.set_yticks([])
    eixo.set_frame_on(False)
    salvar_figura(figura, destino, "04_regra_propagacao")


def salvar_dados(resultado, destino):
    with (destino / "evolucao.csv").open("w", newline="", encoding="utf-8") as arquivo:
        escritor = csv.writer(arquivo)
        escritor.writerow(
            [
                "passo",
                "nao_combustiveis",
                "intactas",
                "em_chamas",
                "queimadas",
                "contencao",
                "novas_ignicoes",
                "contencoes_ativadas",
            ]
        )
        for item in resultado["historico"]:
            escritor.writerow(
                [
                    item["passo"],
                    *item["estados"],
                    item["novas_ignicoes"],
                    item["contencoes_ativadas"],
                ]
            )

    (destino / "resumo.json").write_text(
        json.dumps(resultado["resumo"], indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def nome_saida_sequencial(entrada):
    nome = entrada.stem
    if nome.startswith("entrada_seq_"):
        return nome
    if nome.startswith("entrada_"):
        return f"entrada_seq_{nome.removeprefix('entrada_')}"
    return f"entrada_seq_{nome}"


def main():
    parser = argparse.ArgumentParser(
        description="Gera visualizações da simulação de incêndio."
    )
    parser.add_argument("entrada", type=Path, help="arquivo de entrada da simulação")
    parser.add_argument(
        "--saida",
        type=Path,
        help="diretório de saída (padrão: viz/saida/entrada_seq_<nome>)",
    )
    argumentos = parser.parse_args()

    configuracao = ler_entrada(argumentos.entrada.resolve())
    destino = argumentos.saida or BASE_DIR / "saida" / nome_saida_sequencial(
        argumentos.entrada
    )
    destino.mkdir(parents=True, exist_ok=True)

    resultado = simular(configuracao, carregar_biblioteca())
    salvar_dados(resultado, destino)
    grafico_snapshots(resultado, destino)
    grafico_evolucao(resultado, destino)
    grafico_ignicoes(resultado, destino)
    grafico_regra(configuracao, destino)

    resumo = resultado["resumo"]
    print(f"Figuras geradas em: {destino.resolve()}")
    print(f"Passos: {resumo['passos']}")
    print(f"Pico de ignições: {resumo['pico_passo']} {resumo['pico_quantidade']}")
    print(f"Checksum: {resumo['checksum']}")


if __name__ == "__main__":
    main()
