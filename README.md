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

## Execução

As duas versões recebem o caminho do arquivo de entrada como argumento. Por exemplo:

```bash
./fire_seq data/entrada_carga_pequena.txt
./fire_omp data/entrada_carga_pequena.txt
```

Na versão OpenMP, a quantidade de threads é definida pelo valor `T` informado na primeira linha do arquivo de entrada.

Para remover os executáveis gerados:

```bash
make clean
```

## Integrantes

- Felipe de Castro Azambuja — 14675437
- Gabriel de Andrade Abreu — 14571362
- João Pedro Viguini Tolentino Taufner Correa — 14675503
- Matheus Paiva Angarola — 12560982
- Pietra Gullo Salgado Chaves — 14603822
