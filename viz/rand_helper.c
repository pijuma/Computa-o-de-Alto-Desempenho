#define _POSIX_C_SOURCE 200809L

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

void gerar_floresta(int32_t *cobertura, int32_t *umidade, size_t tamanho, unsigned int seed) {
    for (size_t i = 0; i < tamanho; i++) {
        unsigned int valor = rand_r(&seed) % 100;

        if (valor <= 9) {
            cobertura[i] = 0;
        } else if (valor <= 19) {
            cobertura[i] = 1;
        } else if (valor <= 54) {
            cobertura[i] = 2;
        } else {
            cobertura[i] = 3;
        }

        umidade[i] = (int32_t)(rand_r(&seed) % 101);
    }
}

uint64_t calcular_checksum(const int8_t *estado, const int8_t *tempo, size_t tamanho) {
    uint64_t checksum = 0;

    for (size_t i = 0; i < tamanho; i++) {
        checksum = checksum * 31ULL + (uint64_t)estado[i];
        checksum = checksum * 31ULL + (uint64_t)tempo[i];
    }

    return checksum;
}
