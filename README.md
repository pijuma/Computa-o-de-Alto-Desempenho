<h1 align="center">Simulação de Incêndio Florestal</h1>
<p align="center"> Projeto desenvolvido para a disciplina de <b>Computação de Alto Desempenho (SSC0903)</b></p>

<p align="justify">
O trabalho simula a propagação de um incêndio florestal considerando vegetação, umidade, vento, focos iniciais e zonas de contenção. Seu objetivo é desenvolver e comparar as versões sequencial e paralela do algoritmo, avaliando a correção e o desempenho da simulação.</p>


## Compilação

```bash
make
```

Esse comando gera dois executáveis:

- `fire_seq`: versão sequencial;
- `fire_omp`: versão paralela com OpenMP.

Para remover os executáveis gerados:

```bash
make clean
```

## Execução

As duas versões recebem **apenas** o caminho do arquivo de entrada como argumento.

### Versão sequencial

```bash
./fire_seq data/entrada_carga_pequena.txt
```

### Versão paralela (OpenMP)

A versão paralela usa `schedule(runtime)` nos laços paralelos. Por isso, o escalonamento é escolhido **no momento da execução**, pela variável de ambiente `OMP_SCHEDULE`, e não por um argumento do programa. Defina a variável na mesma linha do comando:

```bash
OMP_SCHEDULE=static ./fire_omp data/entrada_carga_pequena.txt
```

Outros exemplos:

```bash
OMP_SCHEDULE="static,1024" ./fire_omp data/entrada_carga_pequena.txt
OMP_SCHEDULE="dynamic,64"  ./fire_omp data/entrada_carga_pequena.txt
OMP_SCHEDULE="guided,64"   ./fire_omp data/entrada_carga_pequena.txt
```

O formato é `tipo[,chunk]`, em que `tipo` pode ser `static`, `dynamic`, `guided` ou `auto`, e `chunk` é o tamanho opcional dos blocos de iterações.

Para usar o mesmo schedule em várias execuções seguidas, exporte a variável uma vez no terminal:

```bash
export OMP_SCHEDULE=static
./fire_omp data/entrada_carga_pequena.txt
./fire_omp data/entrada_carga_grande.txt
```

> **Padrão:** quando `OMP_SCHEDULE` não é definida, `fire_omp` seleciona `static`. Defina a variável somente quando quiser comparar outra política de escalonamento, como nos exemplos acima.

A versão OpenMP mantém uma única região paralela durante a simulação. As novas ignições, as células em chamas e os cinco estados finais são contados com reduções; a contagem final usa um laço linear `omp for simd`. O relatório descreve as barreiras implícitas, a região `single` e o diagnóstico de vetorização do compilador do cluster.


## Integrantes

- Felipe de Castro Azambuja — 14675437
- Gabriel de Andrade Abreu — 14571362
- João Pedro Viguini Tolentino Taufner Correa — 14675503
- Matheus Paiva Angarola — 12560982
- Pietra Gullo Salgado Chaves — 14603822
