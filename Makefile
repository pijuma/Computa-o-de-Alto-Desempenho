.PHONY: all clean

MPICC ?= mpicc
CC ?= gcc
CFLAGS := -Wall -Wextra -O3 -fopenmp

all: fire_mpi_omp

fire_mpi_omp: fire_mpi_omp.c
	$(MPICC) $(CFLAGS) $< -o $@

fire_seq: fire_seq.c
	$(CC) $(CFLAGS) $< -o $@

fire_omp: fire_omp.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f fire_mpi_omp fire_seq fire_omp
