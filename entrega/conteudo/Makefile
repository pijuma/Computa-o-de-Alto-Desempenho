.PHONY: all clean

CC := gcc
CFLAGS := -Wall -Wextra -O2 -fopenmp

all: fire_seq fire_omp

fire_seq: fire_seq.c
	$(CC) $(CFLAGS) $< -o $@

fire_omp: fire_omp.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f fire_seq fire_omp
