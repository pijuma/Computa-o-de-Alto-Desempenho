# Visualizações

Ferramenta independente que reproduz a simulação apenas para gerar dados e figuras do relatório. Ela não altera, inclui ou executa o `fire_seq.c`, portanto não interfere na medição de desempenho do programa principal.

## Como executar

Na raiz do projeto:

```bash
python3 viz/visualizar.py entrada_carga_pequena.txt
```

As dependências são NumPy, Matplotlib e GCC. Caso necessário:

```bash
python3 -m pip install -r viz/requirements.txt
```

Os resultados sequenciais são salvos em `viz/saida/entrada_seq_<nome>/`, diferenciando-os das futuras visualizações da versão paralela:

- `01_snapshots`: quatro estados em passos igualmente espaçados;
- `02_evolucao_estados`: quantidade de células em cada estado por passo;
- `03_ignicoes_por_passo`: novas ignições e ativações de contenção;
- `04_regra_propagacao`: pesos da vizinhança para o vento informado;
- `evolucao.csv`: dados usados nos gráficos;
- `resumo.json`: métricas finais e checksum.

Cada figura é gerada em PDF e sem título, permitindo que o texto e a legenda sejam definidos diretamente no relatório. Para resultados de desempenho, use sempre o executável principal sem instrumentação; esta ferramenta é destinada somente à produção das figuras.
