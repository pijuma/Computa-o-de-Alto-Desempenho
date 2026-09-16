// Simula de forma paralela, com OpenMP, a propagacao de um incendio em uma matriz
// Considera cobertura, umidade, vento, focos iniciais e zonas de contencao
// Data: 14/09/2026

// Execute com: make && ./fire_omp arquivo_de_entrada

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
    // Gera a cobertura e a umidade das celulas

    for (int i = 0; i < L; i++) {
        for (int j = 0; j < C; j++) {
            long long pos = (long long)i * C + j;

            cobertura[pos] = tipo_cobertura(rand_r(seed) % 100);
            umidade[pos] = rand_r(seed) % 101;
        }
    }
}

void ativar_contencoes(
    int passo,
    int* ativacao,
    int* estado_atual,
    int L,
    int C
) {
    // Ativa em paralelo as contencoes do passo atual
    #pragma omp for collapse(2) schedule(runtime)
    for (int i = 0; i < L; i++) {
        for (int j = 0; j < C; j++) {
            long long pos = (long long)i * C + j;
            if (estado_atual[pos] != 1)
                continue;
            if (ativacao[pos] == passo) {
                estado_atual[pos] = 4;
            }
        }
    }
}

bool valid(int i, int j, int L, int C) {
    // Verifica se a posicao pertence a matriz

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

void atualizar_celula(
    int* estado_atual,
    int* tempo_atual,
    int* proximo_estado,
    int* proximo_tempo,
    int linha,
    int coluna,
    int L,
    int C,
    int LIMIAR,
    int* umidade,
    int* cobertura,
    VENTO v
) {
    // Calcula o proximo estado de uma celula

    long long pos = (long long)linha * C + coluna;
    if (!bet(estado_atual[pos], 1, 2)) {
        proximo_estado[pos] = estado_atual[pos];
        proximo_tempo[pos] = tempo_atual[pos];
        return;
    }
    if (estado_atual[pos] == 2) {
        proximo_tempo[pos] = tempo_atual[pos] - 1;
        if (!proximo_tempo[pos]) {
            proximo_estado[pos] = 3;
        } else {
            proximo_estado[pos] = 2;
        }
        return;
    }

    int soma_viz = 0;

    for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
            if (i == j && i == 0)
                continue;

            int vis_i = linha + i, vis_j = j + coluna;
            if (!valid(vis_i, vis_j, L, C))
                continue;
            if (estado_atual[(long long)vis_i * C + vis_j] != 2)
                continue;

            int prop_linha = linha - vis_i, prop_coluna = coluna - vis_j;
            int peso = 7;

            if (abs(prop_linha) + abs(prop_coluna) == 1)
                peso = 10;

            int A = prop_linha * v.vento_linha + prop_coluna * v.vento_coluna;

            soma_viz += max(1, peso + v.V_vento * A);
        }
    }

    int potencial_cel =
        (soma_viz * fator_combustivel(cobertura[pos]) * (100 - umidade[pos]));
    potencial_cel /= 100;

    if (potencial_cel >= LIMIAR) {
        if (cobertura[pos] == 2)
            proximo_tempo[pos] = 2;
        else if (cobertura[pos] == 3)
            proximo_tempo[pos] = 4;
        proximo_estado[pos] = 2;
    } else {
        proximo_estado[pos] = 1;
        proximo_tempo[pos] = 0;
    }
}

void trocar_matrizes(
    int** estado_atual,
    int** proximo_estado,
    int** tempo_atual,
    int** proximo_tempo
) {
    // Alterna as matrizes da simulacao

    int* aux;

    aux = *estado_atual;
    *estado_atual = *proximo_estado;
    *proximo_estado = aux;

    aux = *tempo_atual;
    *tempo_atual = *proximo_tempo;
    *proximo_tempo = aux;
}

Estatisticas contar_estados(int* estado_atual, long long tamanho) {
    // Conta as celulas em cada estado

    Estatisticas stats = {0, 0, 0, 0, 0};

    for (long long i = 0; i < tamanho; i++) {
        switch (estado_atual[i]) {
            case 0:
                stats.nao_combustiveis++;
                break;
            case 1:
                stats.intactas++;
                break;
            case 2:
                stats.em_chamas++;
                break;
            case 3:
                stats.queimadas++;
                break;
            case 4:
                stats.contencao++;
                break;
            default:
                break;
        }
    }

    return stats;
}

int main(int argc, char* argv[]) {
    // Executa a simulacao de incendio

    if (argc != 2) {
        fprintf(stderr, "Entrada incorreta");
        return 1;
    }

    FILE* entrada = fopen(argv[1], "r");

    if (entrada == NULL) {
        fprintf(stderr, "Erro ao abrir o entrada.\n");
        return 1;
    }

    int L, C, P, T;
    unsigned int seed;
    int limiar;

    int lidos = fscanf(entrada, "%d %d %d %d %u %d", &L, &C, &P, &T, &seed, &limiar);

    if (lidos != 6) {
        fprintf(stderr, "Primeira linha invalida\n");
        fclose(entrada);
        return 1;
    }

    if (limiar <= 0 || T <= 0 || P < 0 || C <= 0 || L <= 0) {
        fprintf(stderr, "Primeira linha invalida\n");
        fclose(entrada);
        return 1;
    }

    VENTO vento;

    int lidos_2 = fscanf(
        entrada, "%d %d %d", &vento.vento_linha, &vento.vento_coluna, &vento.V_vento
    );

    if (lidos_2 != 3) {
        fprintf(stderr, "Segunda linha invalida\n");
        fclose(entrada);
        return 1;
    }

    if (vento.vento_linha == 0 && vento.vento_coluna == 0) {
        fprintf(stderr, "Segunda linha invalida\n");
        fclose(entrada);
        return 1;
    }

    if (vento.vento_linha < -1 || vento.vento_linha > 1 || vento.vento_coluna < -1 ||
        vento.vento_coluna > 1 || vento.V_vento < 0 || vento.V_vento > 5) {
        fprintf(stderr, "Segunda linha invalida\n");
        fclose(entrada);
        return 1;
    }

    int F, Z;

    if (fscanf(entrada, "%d %d", &F, &Z) != 2) {
        fprintf(stderr, "Terceira linha invalida\n");
        fclose(entrada);
        return 1;
    }

    if (F < 0 || Z < 0) {
        fprintf(stderr, "Terceira linha invalida\n");
        fclose(entrada);
        return 1;
    }

    FOCO* focos = NULL;
    if (F > 0)
        focos = malloc((size_t)F * sizeof(FOCO));
    long long tamanho = (long long)L * C;

    bool* tem_foco = calloc(tamanho, sizeof(bool));

    if ((F > 0 && focos == NULL) || tem_foco == NULL) {
        fprintf(stderr, "Erro de alocacao\n");
        free(focos);
        free(tem_foco);
        fclose(entrada);
        return 1;
    }

    for (int i = 0; i < F; i++) {
        if (fscanf(entrada, "%d %d", &focos[i].linha, &focos[i].coluna) != 2) {
            fprintf(stderr, "Foco invalido\n");
            free(focos);
            free(tem_foco);
            fclose(entrada);
            return 1;
        }
        if (!bet(focos[i].linha, 0, L - 1)) {
            fprintf(stderr, "Foco invalido\n");
            free(focos);
            free(tem_foco);
            fclose(entrada);
            return 1;
        }
        if (!bet(focos[i].coluna, 0, C - 1)) {
            fprintf(stderr, "Foco invalido\n");
            free(focos);
            free(tem_foco);
            fclose(entrada);
            return 1;
        }
        long long pos = (long long)focos[i].linha * C + focos[i].coluna;
        if (tem_foco[pos] == 1) {
            fprintf(stderr, "Foco invalido\n");
            free(focos);
            free(tem_foco);
            fclose(entrada);
            return 1;
        }
        tem_foco[pos] = 1;
    }

    ZONA* zonas = NULL;
    if (Z > 0)
        zonas = malloc((size_t)Z * sizeof(ZONA));

    int* ativacao = malloc((size_t)tamanho * sizeof(int));

    if ((Z > 0 && zonas == NULL) || ativacao == NULL) {
        fprintf(stderr, "Erro de alocacao\n");
        free(focos);
        free(tem_foco);
        free(zonas);
        free(ativacao);
        fclose(entrada);
        return 1;
    }

    for (long long i = 0; i < tamanho; i++)
        ativacao[i] = -1;

    for (int i = 0; i < Z; i++) {
        if (fscanf(
                entrada,
                "%d %d %d %d %d",
                &zonas[i].passo,
                &zonas[i].linha_inicio,
                &zonas[i].coluna_inicio,
                &zonas[i].linha_fim,
                &zonas[i].coluna_fim
            ) != 5) {
            fprintf(stderr, "Zona invalida.\n");
            free(focos);
            free(zonas);
            free(tem_foco);
            free(ativacao);
            fclose(entrada);
            return 1;
        }
        if (!bet(zonas[i].passo, 0, P - 1) ||
            !bet(zonas[i].linha_inicio, 0, zonas[i].linha_fim) ||
            !bet(zonas[i].linha_fim, zonas[i].linha_inicio, L - 1) ||
            !bet(zonas[i].coluna_inicio, 0, zonas[i].coluna_fim) ||
            !bet(zonas[i].coluna_fim, zonas[i].coluna_inicio, C - 1)) {
            fprintf(stderr, "Zona invalida.\n");
            free(focos);
            free(zonas);
            free(tem_foco);
            free(ativacao);
            fclose(entrada);
            return 1;
        }

        for (int j = zonas[i].linha_inicio; j <= zonas[i].linha_fim; j++) {
            for (int k = zonas[i].coluna_inicio; k <= zonas[i].coluna_fim; k++) {
                long long pos = (long long)j * C + k;
                if (ativacao[pos] == -1)
                    ativacao[pos] = zonas[i].passo;
                ativacao[pos] = min(ativacao[pos], zonas[i].passo);
            }
        }
    }

    int* cobertura = malloc((size_t)tamanho * sizeof(int));
    int* umidade = malloc((size_t)tamanho * sizeof(int));

    if (cobertura == NULL || umidade == NULL) {
        fprintf(stderr, "Erro de alocacao\n");
        free(focos);
        free(tem_foco);
        free(zonas);
        free(ativacao);
        free(cobertura);
        free(umidade);
        fclose(entrada);
        return 1;
    }

    gerar_caracter_celula(cobertura, umidade, &seed, L, C);

    for (int i = 0; i < L; i++) {
        for (int j = 0; j < C; j++) {
            long long pos = (long long)i * C + j;
            if (tem_foco[pos] && cobertura[pos] <= 1) {
                fprintf(stderr, "Foco em área não combustivel.\n");
                free(focos);
                free(zonas);
                free(cobertura);
                free(tem_foco);
                free(ativacao);
                free(umidade);
                fclose(entrada);
                return 1;
            }
        }
    }

    int* estado_atual = calloc(tamanho, sizeof(int));
    int* proximo_estado = malloc((size_t)tamanho * sizeof(int));
    int* tempo_atual = calloc(tamanho, sizeof(int));
    int* proximo_tempo = calloc(tamanho, sizeof(int));

    if (estado_atual == NULL || proximo_estado == NULL || tempo_atual == NULL ||
        proximo_tempo == NULL) {
        fprintf(stderr, "Erro de alocacao\n");
        free(focos);
        free(tem_foco);
        free(zonas);
        free(ativacao);
        free(cobertura);
        free(umidade);
        free(estado_atual);
        free(proximo_estado);
        free(tempo_atual);
        free(proximo_tempo);
        fclose(entrada);
        return 1;
    }

    long long int celulas_chama = 0;
    int pico_ignicao = -1;
    long long qtd_pico = 0;
    long long total_ignicoes = 0;
    int passos_executados = 0;

    long long combustiveis_inicio = 0;
    int p = 0;

    for (long long i = 0; i < tamanho; i++) {
        if (cobertura[i] <= 1) {
            estado_atual[i] = 0;
        } else {
            estado_atual[i] = 1;
            combustiveis_inicio++;
        }

        tempo_atual[i] = 0;

        if (tem_foco[i]) {
            celulas_chama++;
            estado_atual[i] = 2;
            tempo_atual[i] = (cobertura[i] == 2 ? 2 : 4);
        }
    }

    double start, end;
    start = omp_get_wtime();


    long long int novas ; 


    // Executa cada passo coletivamente com uma equipe de T threads
    #pragma omp parallel num_threads(T) default(none) firstprivate(p) shared(L, C, P, limiar, vento, cobertura, umidade, ativacao, \
       estado_atual, proximo_estado, tempo_atual, proximo_tempo, \
       celulas_chama, qtd_pico, pico_ignicao, total_ignicoes, \
       passos_executados, novas)
    {
        for (; p < P && celulas_chama > 0; p++) {
            ativar_contencoes(p, ativacao, estado_atual, L, C);

            // Distribui a atualizacao das celulas entre as threads
            #pragma omp for schedule(runtime) collapse(2)
            for (int linha = 0; linha < L; linha++) {
                for (int coluna = 0; coluna < C; coluna++) {
                    atualizar_celula(
                        estado_atual,
                        tempo_atual,
                        proximo_estado,
                        proximo_tempo,
                        linha,
                        coluna,
                        L,
                        C,
                        limiar,
                        umidade,
                        cobertura,
                        vento
                    );
                }
            }

            // Reinicia os acumuladores compartilhados antes da reducao
            #pragma omp single
            {
                novas = 0;
                celulas_chama = 0;
            }

            // Soma em paralelo as novas ignicoes e as celulas ainda em chamas
            #pragma omp for collapse(2) reduction(+:novas, celulas_chama) schedule(runtime)
            for (int i = 0; i < L; i++) {
                for (int j = 0; j < C; j++) {
                    long long pos = (long long)i * C + j;
                    if (estado_atual[pos] == 1 && proximo_estado[pos] == 2)
                        novas++;
                    if (proximo_estado[pos] == 2)
                        celulas_chama++;
                }
            }
            
            // Uma unica thread atualiza as estatisticas do pico de ignicoes
            // e consolida o passo e alterna as matrizes
            #pragma omp single
            {
                if (novas > qtd_pico) {
                    qtd_pico = novas;
                    pico_ignicao = p;
                }

                total_ignicoes += novas;
                trocar_matrizes(&estado_atual, &proximo_estado, &tempo_atual, &proximo_tempo);
                passos_executados++;
            }
            
            if (!celulas_chama)
                break;
        }
    }

    
    Estatisticas resp = contar_estados(estado_atual, tamanho);

    end = omp_get_wtime();

    unsigned long long checksum = 0;

    for (long long i = 0; i < tamanho; i++) {
        checksum = checksum * 31ULL + (unsigned long long)estado_atual[i];
        checksum = checksum * 31ULL + (unsigned long long)tempo_atual[i];
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
    printf("tempo: %.6f\n", end - start);

    free(focos);
    free(zonas);
    free(cobertura);
    free(umidade);
    free(estado_atual);
    free(proximo_estado);
    free(tempo_atual);
    free(proximo_tempo);
    free(ativacao);
    fclose(entrada);
    free(tem_foco);

    return 0;
}
