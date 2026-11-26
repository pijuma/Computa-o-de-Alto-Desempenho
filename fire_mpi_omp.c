// Simula de forma paralela hibrida (C + MPI + OpenMP) a propagacao de um incendio em uma matriz
// Considera cobertura, umidade, vento, focos iniciais e zonas de contencao
// Data: 23/11/2026
//
// Execute com:
//   mpirun --hostfile <arq_hosts> -np <NPrc> ./fire_mpi_omp <arq_entrada>

#include <limits.h>
#include <mpi.h>
#include <omp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// Representa a posicao de um foco inicial de incendio
typedef struct {
    int linha, coluna;
} FOCO;

// Armazena a direcao e a intensidade do vento
typedef struct {
    int vento_linha, vento_coluna, V_vento;
} VENTO;

// Define uma zona de contencao e seu passo de ativacao
typedef struct {
    int passo;
    int linha_inicio, coluna_inicio, linha_fim, coluna_fim;
} ZONA;

// Reune a quantidade de celulas em cada estado da simulacao
typedef struct {
    long long nao_combustiveis;
    long long intactas;
    long long em_chamas;
    long long queimadas;
    long long contencao;
} Estatisticas;

int max(int a, int b) {
    // Retorna o maior valor

    return a > b ? a : b;
}

int min(int a, int b) {
    // Retorna o menor valor

    return a < b ? a : b;
}

bool bet(int a, int b, int c) {
    // Verifica se o valor pertence ao intervalo

    if (a >= b && a <= c)
        return 1;

    return 0;
}

int tipo_cobertura(int val) {
    // Classifica o tipo de cobertura

    if (val <= 9)
        return 0;

    if (val <= 19)
        return 1;

    if (val <= 54)
        return 2;

    return 3;
}

void gerar_caracter_celula(
    int* cobertura,
    int* umidade,
    unsigned int* seed,
    int L,
    int C
) {
    // Gera a cobertura e a umidade das celulas em ordem linear estrita

    for (int i = 0; i < L; i++) {
        for (int j = 0; j < C; j++) {
            long long pos = (long long)i * C + j;

            cobertura[pos] = tipo_cobertura(rand_r(seed) % 100);
            umidade[pos] = rand_r(seed) % 101;
        }
    }
}

void construir_mapa_ativacao_tasks(
    int* ativacao,
    int L,
    int C,
    const ZONA* zonas,
    int Z,
    int T
) {
    // Constroi o mapa de contencao antes do trecho cronometrado usando omp tasks
    // Cada task inicializa e preenche um bloco disjunto de linhas, garantindo escritor unico

    enum { BLOCO_LINHAS = 64 };

    #pragma omp parallel num_threads(T) default(none) shared(ativacao, L, C, zonas, Z)
    {
        #pragma omp single
        {
            for (int r0 = 0; r0 < L;) {
                int r1 = r0 + min(BLOCO_LINHAS, L - r0) - 1;
                #pragma omp task firstprivate(r0, r1) shared(ativacao, C, zonas, Z)
                {
                    for (int r = r0; r <= r1; r++) {
                        long long base = (long long)r * C;
                        for (int c = 0; c < C; c++) {
                            ativacao[base + c] = -1;
                        }
                    }

                    for (int i = 0; i < Z; i++) {
                        int li = max(r0, zonas[i].linha_inicio);
                        int lf = min(r1, zonas[i].linha_fim);
                        if (li > lf)
                            continue;
                        for (int r = li; r <= lf; r++) {
                            long long base = (long long)r * C;
                            for (int c = zonas[i].coluna_inicio; c <= zonas[i].coluna_fim; c++) {
                                long long pos = base + c;
                                if (ativacao[pos] == -1 || zonas[i].passo < ativacao[pos]) {
                                    ativacao[pos] = zonas[i].passo;
                                }
                            }
                        }
                    }
                }
                r0 = r1 + 1;
            }
            #pragma omp taskwait
        }
    }
}

void ativar_contencoes_local(
    int passo,
    const int* ativacao_local,
    int* estado_atual_ext,
    int local_L,
    int C
) {
    // Ativa em paralelo as contencoes do passo atual na faixa local (linhas 1 .. local_L no buffer estendido)

    #pragma omp for collapse(2) schedule(runtime)
    for (int i = 0; i < local_L; i++) {
        for (int j = 0; j < C; j++) {
            long long pos_local = (long long)i * C + j;
            long long pos_ext = (long long)(i + 1) * C + j;
            if (estado_atual_ext[pos_ext] != 1)
                continue;
            if (ativacao_local[pos_local] == passo) {
                estado_atual_ext[pos_ext] = 4;
            }
        }
    }
}

bool valid_global(int i, int j, int L, int C) {
    // Verifica se a posicao global pertence a matriz

    return i >= 0 && j >= 0 && i < L && j < C;
}

int fator_combustivel(int val) {
    // Retorna o fator de combustivel

    if (val == 2)
        return 8;

    if (val == 3)
        return 12;

    return 0;
}

void atualizar_celula_local(
    const int* estado_atual_ext,
    const int* tempo_atual_local,
    int* proximo_estado_ext,
    int* proximo_tempo_local,
    int i_local,
    int coluna,
    int row_offset,
    int L,
    int C,
    int LIMIAR,
    const int* umidade_local,
    const int* cobertura_local,
    VENTO v
) {
    // Calcula o proximo estado de uma celula da faixa local usando as linhas fantasmas em estado_atual_ext

    int i_ext = i_local + 1;
    int linha_global = row_offset + i_local;
    long long pos_local = (long long)i_local * C + coluna;
    long long pos_ext = (long long)i_ext * C + coluna;

    int estado_cel = estado_atual_ext[pos_ext];

    if (!bet(estado_cel, 1, 2)) {
        proximo_estado_ext[pos_ext] = estado_cel;
        proximo_tempo_local[pos_local] = tempo_atual_local[pos_local];
        return;
    }
    if (estado_cel == 2) {
        int novo_tempo = tempo_atual_local[pos_local] - 1;
        proximo_tempo_local[pos_local] = novo_tempo;
        if (!novo_tempo) {
            proximo_estado_ext[pos_ext] = 3;
        } else {
            proximo_estado_ext[pos_ext] = 2;
        }
        return;
    }

    int soma_viz = 0;

    for (int di = -1; di <= 1; di++) {
        for (int dj = -1; dj <= 1; dj++) {
            if (di == 0 && dj == 0)
                continue;

            int vis_global_i = linha_global + di;
            int vis_j = coluna + dj;
            if (!valid_global(vis_global_i, vis_j, L, C))
                continue;

            long long vis_ext_i = (long long)i_ext + di;
            if (estado_atual_ext[vis_ext_i * C + vis_j] != 2)
                continue;

            int prop_linha = -di;
            int prop_coluna = -dj;
            int peso = (abs(prop_linha) + abs(prop_coluna) == 1) ? 10 : 7;

            int A = prop_linha * v.vento_linha + prop_coluna * v.vento_coluna;

            soma_viz += max(1, peso + v.V_vento * A);
        }
    }

    int potencial_cel =
        (soma_viz * fator_combustivel(cobertura_local[pos_local]) * (100 - umidade_local[pos_local]));
    potencial_cel /= 100;

    if (potencial_cel >= LIMIAR) {
        if (cobertura_local[pos_local] == 2)
            proximo_tempo_local[pos_local] = 2;
        else if (cobertura_local[pos_local] == 3)
            proximo_tempo_local[pos_local] = 4;
        proximo_estado_ext[pos_ext] = 2;
    } else {
        proximo_estado_ext[pos_ext] = 1;
        proximo_tempo_local[pos_local] = 0;
    }
}

void trocar_halo_estados(
    int* estado_atual_ext,
    int local_L,
    int C,
    int up_partner,
    int down_partner
) {
    // Troca somente as linhas de estado na fronteira entre processos vizinhos

    if (local_L <= 0)
        return;

    MPI_Sendrecv(
        estado_atual_ext + C, C, MPI_INT, up_partner, 10,
        estado_atual_ext + ((long long)local_L + 1) * C, C, MPI_INT, down_partner, 10,
        MPI_COMM_WORLD, MPI_STATUS_IGNORE
    );
    MPI_Sendrecv(
        estado_atual_ext + (long long)local_L * C, C, MPI_INT, down_partner, 20,
        estado_atual_ext, C, MPI_INT, up_partner, 20,
        MPI_COMM_WORLD, MPI_STATUS_IGNORE
    );
}

void reduzir_passo_mpi(
    long long novas_local,
    long long chamas_local,
    long long* novas_global,
    long long* chamas_global
) {
    // Soma globalmente as novas ignicoes e as celulas em chamas do passo atual

    long long local_pair[2] = {novas_local, chamas_local};
    long long global_pair[2] = {0, 0};
    MPI_Allreduce(local_pair, global_pair, 2, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);
    *novas_global = global_pair[0];
    *chamas_global = global_pair[1];
}

void trocar_matrizes(
    int** estado_atual,
    int** proximo_estado,
    int** tempo_atual,
    int** proximo_tempo
) {
    // Alterna os buffers da simulacao

    int* aux;

    aux = *estado_atual;
    *estado_atual = *proximo_estado;
    *proximo_estado = aux;

    aux = *tempo_atual;
    *tempo_atual = *proximo_tempo;
    *proximo_tempo = aux;
}

int main(int argc, char* argv[]) {
    // Executa a simulacao hibrida MPI + OpenMP de propagacao de incendio

    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);

    int rank = 0, nprocs = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);

    if (provided < MPI_THREAD_FUNNELED) {
        if (rank == 0) {
            fprintf(stderr, "Nivel de suporte a threads MPI insuficiente.\n");
        }
        MPI_Finalize();
        return 1;
    }

    int status_ok = 1;
    int L = 0, C = 0, P = 0, T = 1, limiar = 0;
    unsigned int seed = 0;
    VENTO vento = {0, 0, 0};
    int F = 0, Z = 0;

    long long tamanho = 0;
    long long celulas_chama = 0;
    long long combustiveis_inicio = 0;

    FOCO* focos = NULL;
    bool* tem_foco = NULL;
    ZONA* zonas = NULL;
    int* ativacao_global = NULL;
    int* cobertura_global = NULL;
    int* umidade_global = NULL;
    int* estado_global = NULL;
    int* tempo_global = NULL;

    if (rank == 0) {
        FILE* entrada = NULL;

        if (argc != 2) {
            fprintf(stderr, "Entrada incorreta\n");
            status_ok = 0;
        } else {
            entrada = fopen(argv[1], "r");
            if (entrada == NULL) {
                fprintf(stderr, "Erro ao abrir o entrada.\n");
                status_ok = 0;
            }
        }

        if (status_ok) {
            int lidos = fscanf(entrada, "%d %d %d %d %u %d", &L, &C, &P, &T, &seed, &limiar);
            if (lidos != 6 || limiar <= 0 || T <= 0 || P < 0 || C <= 0 || L <= 0) {
                fprintf(stderr, "Primeira linha invalida\n");
                status_ok = 0;
            }
        }

        if (status_ok) {
            long long total_cells = (long long)L * (long long)C;
            if (total_cells <= 0 || total_cells > (long long)INT_MAX) {
                fprintf(stderr, "Dimensoes excedem o limite suportado pelo MPI.\n");
                status_ok = 0;
            } else {
                tamanho = total_cells;
            }
        }

        if (status_ok) {
            int lidos_2 = fscanf(
                entrada, "%d %d %d", &vento.vento_linha, &vento.vento_coluna, &vento.V_vento
            );
            if (lidos_2 != 3 ||
                (vento.vento_linha == 0 && vento.vento_coluna == 0) ||
                vento.vento_linha < -1 || vento.vento_linha > 1 ||
                vento.vento_coluna < -1 || vento.vento_coluna > 1 ||
                vento.V_vento < 0 || vento.V_vento > 5) {
                fprintf(stderr, "Segunda linha invalida\n");
                status_ok = 0;
            }
        }

        if (status_ok) {
            if (fscanf(entrada, "%d %d", &F, &Z) != 2 || F < 0 || Z < 0) {
                fprintf(stderr, "Terceira linha invalida\n");
                status_ok = 0;
            }
        }

        if (status_ok) {
            if (F > 0)
                focos = malloc((size_t)F * sizeof(FOCO));
            tem_foco = calloc((size_t)tamanho, sizeof(bool));

            if ((F > 0 && focos == NULL) || tem_foco == NULL) {
                fprintf(stderr, "Erro de alocacao\n");
                status_ok = 0;
            }
        }

        if (status_ok) {
            for (int i = 0; i < F; i++) {
                if (fscanf(entrada, "%d %d", &focos[i].linha, &focos[i].coluna) != 2 ||
                    !bet(focos[i].linha, 0, L - 1) ||
                    !bet(focos[i].coluna, 0, C - 1)) {
                    fprintf(stderr, "Foco invalido\n");
                    status_ok = 0;
                    break;
                }
                long long pos = (long long)focos[i].linha * C + focos[i].coluna;
                if (tem_foco[pos] == 1) {
                    fprintf(stderr, "Foco invalido\n");
                    status_ok = 0;
                    break;
                }
                tem_foco[pos] = 1;
            }
        }

        if (status_ok) {
            if (Z > 0)
                zonas = malloc((size_t)Z * sizeof(ZONA));
            ativacao_global = malloc((size_t)tamanho * sizeof(int));

            if ((Z > 0 && zonas == NULL) || ativacao_global == NULL) {
                fprintf(stderr, "Erro de alocacao\n");
                status_ok = 0;
            }
        }

        if (status_ok) {
            for (int i = 0; i < Z; i++) {
                if (fscanf(
                        entrada,
                        "%d %d %d %d %d",
                        &zonas[i].passo,
                        &zonas[i].linha_inicio,
                        &zonas[i].coluna_inicio,
                        &zonas[i].linha_fim,
                        &zonas[i].coluna_fim
                    ) != 5 ||
                    !bet(zonas[i].passo, 0, P - 1) ||
                    !bet(zonas[i].linha_inicio, 0, zonas[i].linha_fim) ||
                    !bet(zonas[i].linha_fim, zonas[i].linha_inicio, L - 1) ||
                    !bet(zonas[i].coluna_inicio, 0, zonas[i].coluna_fim) ||
                    !bet(zonas[i].coluna_fim, zonas[i].coluna_inicio, C - 1)) {
                    fprintf(stderr, "Zona invalida.\n");
                    status_ok = 0;
                    break;
                }
            }
        }

        if (status_ok) {
            construir_mapa_ativacao_tasks(ativacao_global, L, C, zonas, Z, T);

            cobertura_global = malloc((size_t)tamanho * sizeof(int));
            umidade_global = malloc((size_t)tamanho * sizeof(int));

            if (cobertura_global == NULL || umidade_global == NULL) {
                fprintf(stderr, "Erro de alocacao\n");
                status_ok = 0;
            }
        }

        if (status_ok) {
            gerar_caracter_celula(cobertura_global, umidade_global, &seed, L, C);

            for (int i = 0; i < L && status_ok; i++) {
                for (int j = 0; j < C; j++) {
                    long long pos = (long long)i * C + j;
                    if (tem_foco[pos] && cobertura_global[pos] <= 1) {
                        fprintf(stderr, "Foco em área não combustivel.\n");
                        status_ok = 0;
                        break;
                    }
                }
            }
        }

        if (status_ok) {
            estado_global = calloc((size_t)tamanho, sizeof(int));
            tempo_global = calloc((size_t)tamanho, sizeof(int));

            if (estado_global == NULL || tempo_global == NULL) {
                fprintf(stderr, "Erro de alocacao\n");
                status_ok = 0;
            } else {
                for (long long i = 0; i < tamanho; i++) {
                    if (cobertura_global[i] <= 1) {
                        estado_global[i] = 0;
                    } else {
                        estado_global[i] = 1;
                        combustiveis_inicio++;
                    }

                    tempo_global[i] = 0;

                    if (tem_foco[i]) {
                        celulas_chama++;
                        estado_global[i] = 2;
                        tempo_global[i] = (cobertura_global[i] == 2 ? 2 : 4);
                    }
                }
            }
        }

        if (entrada != NULL) {
            fclose(entrada);
        }
        free(focos);
        free(tem_foco);
        free(zonas);
    }

    // Coletiva 1: compartilha status da validacao e preparacao do rank 0
    MPI_Bcast(&status_ok, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!status_ok) {
        free(ativacao_global);
        free(cobertura_global);
        free(umidade_global);
        free(estado_global);
        free(tempo_global);
        MPI_Finalize();
        return 1;
    }

    // Coletiva 2: distribui os parametros escalares da simulacao
    int meta_ints[8];
    if (rank == 0) {
        meta_ints[0] = L;
        meta_ints[1] = C;
        meta_ints[2] = P;
        meta_ints[3] = T;
        meta_ints[4] = limiar;
        meta_ints[5] = vento.vento_linha;
        meta_ints[6] = vento.vento_coluna;
        meta_ints[7] = vento.V_vento;
    }
    MPI_Bcast(meta_ints, 8, MPI_INT, 0, MPI_COMM_WORLD);
    L = meta_ints[0];
    C = meta_ints[1];
    P = meta_ints[2];
    T = meta_ints[3];
    limiar = meta_ints[4];
    vento.vento_linha = meta_ints[5];
    vento.vento_coluna = meta_ints[6];
    vento.V_vento = meta_ints[7];
    tamanho = (long long)L * C;

    long long meta_ll[2];
    if (rank == 0) {
        meta_ll[0] = celulas_chama;
        meta_ll[1] = combustiveis_inicio;
    }
    MPI_Bcast(meta_ll, 2, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    celulas_chama = meta_ll[0];
    combustiveis_inicio = meta_ll[1];

    // Particionamento 1D por faixas contiguas de linhas
    int* sendcounts = malloc((size_t)nprocs * sizeof(int));
    int* displs = malloc((size_t)nprocs * sizeof(int));
    int base_rows = L / nprocs;
    int rem_rows = L % nprocs;
    int local_L = base_rows + (rank < rem_rows);
    int row_offset = rank * base_rows + min(rank, rem_rows);
    long long local_cells = (long long)local_L * C;
    long long ext_cells = ((long long)local_L + 2) * C;

    size_t alloc_local = (local_cells > 0) ? (size_t)local_cells : 1;
    int* cobertura_local = malloc(alloc_local * sizeof(int));
    int* umidade_local = malloc(alloc_local * sizeof(int));
    int* ativacao_local = malloc(alloc_local * sizeof(int));
    int* tempo_atual = calloc(alloc_local, sizeof(int));
    int* proximo_tempo = calloc(alloc_local, sizeof(int));
    int* estado_atual_ext = calloc((size_t)ext_cells, sizeof(int));
    int* proximo_estado_ext = calloc((size_t)ext_cells, sizeof(int));

    int local_alloc_ok =
        (sendcounts != NULL && displs != NULL &&
         cobertura_local != NULL && umidade_local != NULL && ativacao_local != NULL &&
         tempo_atual != NULL && proximo_tempo != NULL &&
         estado_atual_ext != NULL && proximo_estado_ext != NULL)
            ? 1
            : 0;

    int global_alloc_ok = 0;
    MPI_Allreduce(&local_alloc_ok, &global_alloc_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    if (!global_alloc_ok) {
        if (rank == 0) {
            fprintf(stderr, "Erro de alocacao nos processos MPI.\n");
        }
        free(sendcounts);
        free(displs);
        free(cobertura_local);
        free(umidade_local);
        free(ativacao_local);
        free(tempo_atual);
        free(proximo_tempo);
        free(estado_atual_ext);
        free(proximo_estado_ext);
        free(ativacao_global);
        free(cobertura_global);
        free(umidade_global);
        free(estado_global);
        free(tempo_global);
        MPI_Finalize();
        return 1;
    }

    for (int r = 0; r < nprocs; r++) {
        int rows = base_rows + (r < rem_rows);
        int offset = r * base_rows + min(r, rem_rows);
        sendcounts[r] = rows * C;
        displs[r] = offset * C;
    }

    // Coletivas 3..7: distribui as fatias de cobertura, umidade, ativacao, estado inicial e tempo inicial
    MPI_Scatterv(
        cobertura_global, sendcounts, displs, MPI_INT,
        cobertura_local, sendcounts[rank], MPI_INT, 0, MPI_COMM_WORLD
    );
    MPI_Scatterv(
        umidade_global, sendcounts, displs, MPI_INT,
        umidade_local, sendcounts[rank], MPI_INT, 0, MPI_COMM_WORLD
    );
    MPI_Scatterv(
        ativacao_global, sendcounts, displs, MPI_INT,
        ativacao_local, sendcounts[rank], MPI_INT, 0, MPI_COMM_WORLD
    );
    MPI_Scatterv(
        estado_global, sendcounts, displs, MPI_INT,
        estado_atual_ext + C, sendcounts[rank], MPI_INT, 0, MPI_COMM_WORLD
    );
    MPI_Scatterv(
        tempo_global, sendcounts, displs, MPI_INT,
        tempo_atual, sendcounts[rank], MPI_INT, 0, MPI_COMM_WORLD
    );

    // Cobertura, umidade e ativacao globais nao sao mais necessarias no rank 0
    free(cobertura_global);
    free(umidade_global);
    free(ativacao_global);

    // Determina os vizinhos ativos acima e abaixo para troca de linhas fantasmas (halos)
    int up_partner = MPI_PROC_NULL;
    int down_partner = MPI_PROC_NULL;
    if (local_L > 0) {
        if (rank > 0)
            up_partner = rank - 1;
        if (rank + 1 < min(L, nprocs))
            down_partner = rank + 1;
    }

    // Configura a equipe OpenMP
    omp_set_dynamic(0);
    if (getenv("OMP_SCHEDULE") == NULL)
        omp_set_schedule(omp_sched_static, 0);

    int pico_ignicao = -1;
    long long qtd_pico = 0;
    long long total_ignicoes = 0;
    int passos_executados = 0;
    int p = 0;

    long long novas_local = 0;
    long long chamas_local = 0;

    long long nao_combustiveis_local = 0;
    long long intactas_local = 0;
    long long em_chamas_local = 0;
    long long queimadas_local = 0;
    long long contencao_local = 0;

    // Sincroniza todos os processos antes de iniciar o cronometro do nucleo
    MPI_Barrier(MPI_COMM_WORLD);
    double start = omp_get_wtime();

    // Regiao paralela persistente com T threads por processo MPI
    #pragma omp parallel num_threads(T) default(none) firstprivate(p) shared( \
        L, C, P, limiar, vento, local_L, row_offset, local_cells, \
        up_partner, down_partner, cobertura_local, umidade_local, ativacao_local, \
        estado_atual_ext, proximo_estado_ext, tempo_atual, proximo_tempo, \
        celulas_chama, qtd_pico, pico_ignicao, total_ignicoes, \
        passos_executados, novas_local, chamas_local, \
        nao_combustiveis_local, intactas_local, em_chamas_local, \
        queimadas_local, contencao_local \
    )
    {
        for (; p < P && celulas_chama > 0; p++) {
            // 1. Ativa as zonas programadas para o passo p na faixa local
            ativar_contencoes_local(p, ativacao_local, estado_atual_ext, local_L, C);

            // 2. Troca as linhas fantasmas de estado_atual_ext apos a ativacao das zonas
            // Somente a thread 0 (que inicializou o MPI em MPI_THREAD_FUNNELED) realiza chamadas MPI
            if (omp_get_thread_num() == 0) {
                trocar_halo_estados(estado_atual_ext, local_L, C, up_partner, down_partner);
                novas_local = 0;
                chamas_local = 0;
            }
            #pragma omp barrier

            // 3. Calcula o proximo estado e o proximo tempo de todas as celulas da faixa local
            #pragma omp for schedule(runtime) collapse(2)
            for (int i_local = 0; i_local < local_L; i_local++) {
                for (int coluna = 0; coluna < C; coluna++) {
                    atualizar_celula_local(
                        estado_atual_ext,
                        tempo_atual,
                        proximo_estado_ext,
                        proximo_tempo,
                        i_local,
                        coluna,
                        row_offset,
                        L,
                        C,
                        limiar,
                        umidade_local,
                        cobertura_local,
                        vento
                    );
                }
            }

            // 4. Soma em paralelo as novas ignicoes e as celulas em chamas na faixa local
            #pragma omp for simd reduction(+:novas_local, chamas_local) schedule(runtime)
            for (long long pos = 0; pos < local_cells; pos++) {
                long long pos_ext = pos + C;
                if (estado_atual_ext[pos_ext] == 1 && proximo_estado_ext[pos_ext] == 2)
                    novas_local++;
                if (proximo_estado_ext[pos_ext] == 2)
                    chamas_local++;
            }

            // 5. Reducao global MPI, atualizacao de estatisticas, troca de buffers e verificacao de parada
            if (omp_get_thread_num() == 0) {
                long long novas_global = 0;
                reduzir_passo_mpi(novas_local, chamas_local, &novas_global, &celulas_chama);

                if (novas_global > qtd_pico) {
                    qtd_pico = novas_global;
                    pico_ignicao = p;
                }

                total_ignicoes += novas_global;
                trocar_matrizes(&estado_atual_ext, &proximo_estado_ext, &tempo_atual, &proximo_tempo);
                passos_executados++;
            }
            #pragma omp barrier

            if (!celulas_chama)
                break;
        }

        // Contagem local em paralelo dos cinco estados finais ainda dentro da equipe persistente
        #pragma omp for simd schedule(runtime) reduction(+:nao_combustiveis_local, intactas_local, em_chamas_local, queimadas_local, contencao_local)
        for (long long i = 0; i < local_cells; i++) {
            int estado = estado_atual_ext[i + C];
            nao_combustiveis_local += (estado == 0);
            intactas_local += (estado == 1);
            em_chamas_local += (estado == 2);
            queimadas_local += (estado == 3);
            contencao_local += (estado == 4);
        }
    }

    // Reducao global dos cinco estados finais
    long long contadores_locais[5] = {
        nao_combustiveis_local,
        intactas_local,
        em_chamas_local,
        queimadas_local,
        contencao_local
    };
    long long contadores_globais[5] = {0, 0, 0, 0, 0};
    MPI_Allreduce(contadores_locais, contadores_globais, 5, MPI_LONG_LONG, MPI_SUM, MPI_COMM_WORLD);

    double end = omp_get_wtime();
    double local_elapsed = end - start;
    double max_elapsed = 0.0;
    MPI_Reduce(&local_elapsed, &max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // Reune a configuracao final no rank 0 para calculo do checksum fora do trecho cronometrado
    MPI_Gatherv(
        estado_atual_ext + C, sendcounts[rank], MPI_INT,
        estado_global, sendcounts, displs, MPI_INT, 0, MPI_COMM_WORLD
    );
    MPI_Gatherv(
        tempo_atual, sendcounts[rank], MPI_INT,
        tempo_global, sendcounts, displs, MPI_INT, 0, MPI_COMM_WORLD
    );

    if (rank == 0) {
        Estatisticas resp = {
            contadores_globais[0],
            contadores_globais[1],
            contadores_globais[2],
            contadores_globais[3],
            contadores_globais[4]
        };

        unsigned long long checksum = 0;
        for (long long i = 0; i < tamanho; i++) {
            checksum = checksum * 31ULL + (unsigned long long)estado_global[i];
            checksum = checksum * 31ULL + (unsigned long long)tempo_global[i];
        }

        double percentual_queimado = 0.0;
        double percentual_protegido = 0.0;

        if (combustiveis_inicio > 0) {
            percentual_queimado = 100.0 * (resp.queimadas + resp.em_chamas) / combustiveis_inicio;
            percentual_protegido = 100.0 * resp.contencao / combustiveis_inicio;
        }

        printf("passos: %d\n", passos_executados);
        printf("nao_combustiveis: %lld\n", resp.nao_combustiveis);
        printf("intactas: %lld\n", resp.intactas);
        printf("em_chamas: %lld\n", resp.em_chamas);
        printf("queimadas: %lld\n", resp.queimadas);
        printf("contencao: %lld\n", resp.contencao);
        printf("total_ignicoes: %lld\n", total_ignicoes);
        printf("pico_ignicoes: %d %lld\n", pico_ignicao, qtd_pico);
        printf("percentual_queimado: %.2f\n", percentual_queimado);
        printf("percentual_protegido: %.2f\n", percentual_protegido);
        printf("checksum: %llu\n", checksum);
        printf("tempo: %.6f\n", max_elapsed);
    }

    free(sendcounts);
    free(displs);
    free(cobertura_local);
    free(umidade_local);
    free(ativacao_local);
    free(tempo_atual);
    free(proximo_tempo);
    free(estado_atual_ext);
    free(proximo_estado_ext);
    free(estado_global);
    free(tempo_global);

    MPI_Finalize();
    return 0;
}
