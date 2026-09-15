# Visualizações

Ferramenta independente que reproduz a simulação apenas para gerar dados e figuras do relatório. Ela não altera, inclui ou executa `fire_seq.c` ou `fire_omp.c`, portanto não interfere na medição de desempenho dos programas principais.

## Como executar

Na raiz do projeto, gere a visualização sequencial com:

```bash
python3 viz/visualizar.py data/entrada_carga_pequena.txt
```

Para gerar a visualização da versão OpenMP, use:

```bash
python3 viz/visualizar.py data/entrada_carga_pequena.txt --versao omp
```

As dependências são NumPy, Matplotlib e GCC. Caso necessário:

```bash
python3 -m pip install -r viz/requirements.txt
```

Os resultados são separados por versão:

- sequencial: `viz/saida/entrada_seq_<nome>/`;
- OpenMP: `viz/saida/entrada_omp_<nome>/`.

Cada diretório contém:

- `01_snapshots`: quatro estados em passos igualmente espaçados;
- `02_evolucao_estados`: quantidade de células em cada estado por passo;
- `03_ignicoes_por_passo`: novas ignições e ativações de contenção;
- `04_regra_propagacao`: pesos da vizinhança para o vento informado;
- `evolucao.csv`: dados usados nos gráficos;
- `resumo.json`: métricas finais e checksum.

Cada figura é gerada em PDF e sem título, permitindo que o texto e a legenda sejam definidos diretamente no relatório. Para resultados de desempenho, use sempre o executável principal sem instrumentação; esta ferramenta é destinada somente à produção das figuras.

Como as versões sequencial e OpenMP implementam a mesma simulação determinística, seus dados, gráficos, métricas finais e checksums devem ser iguais para um mesmo arquivo de entrada. O paralelismo afeta apenas o tempo de execução, que não é medido por esta ferramenta.

## Gráficos de desempenho

Para medir as três cargas e gerar gráficos de tempo, speedup, eficiência e comparação de schedules:

```bash
python3 viz/desempenho.py
```

Por padrão, são realizadas três medições com 1, 2, 4, 8, 16 e 32 threads. Os resultados são salvos em `viz/saida/desempenho/`, incluindo um arquivo `medicoes.csv` com os tempos brutos. As quantidades podem ser alteradas, por exemplo:

```bash
python3 viz/desempenho.py --threads 1 2 4 8 --repeticoes 5
```

A comparação usa `static`, `static` com bloco 16, `dynamic` com blocos 16 e 64 e `guided` com bloco 16. As variantes são compiladas em uma pasta temporária; o script não modifica `fire_omp.c`.
