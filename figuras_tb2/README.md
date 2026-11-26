# Figuras do TB2

A partir da raiz do repositório, execute `tectonic -X compile relatorio_lasdpc.tex`. O relatório inclui os arquivos LaTeX desta pasta.

- `dependencias.tex` mostra a ordem das etapas e a sincronização antes da atualização.
- `faixas.tex` mostra a troca de linhas de fronteira.
- `desempenho.tex` inclui os três painéis de desempenho, montados por `graficos.tex`.

Para preencher os painéis, salve `figuras_tb2/desempenho.csv` com o cabeçalho abaixo. Use uma linha por configuração, na ordem desejada no eixo horizontal. Use ponto decimal, segundos para o tempo médio do núcleo, speedup como razão e eficiência em porcentagem (por exemplo, 75 para 75%). Coloque a mesma configuração nas três cargas da linha. Rotule a configuração como `1x2` ou `2x1`.

```csv
configuracao,pequena_tempo_s,pequena_speedup,pequena_eficiencia_pct,media_tempo_s,media_speedup,media_eficiencia_pct,grande_tempo_s,grande_speedup,grande_eficiencia_pct
```

Use médias da campanha LASDPC. Para cada carga, calcule o speedup com o tempo sequencial da mesma carga e a eficiência como 100 vezes o speedup dividido por R×T. Identifique no rótulo as colocações distintas que compartilham R×T. Enquanto o CSV estiver ausente, o PDF mostra os painéis como pendentes.
