.PHONY: all clean

all: fire_seq

fire_seq: fire_seq.c
	gcc -Wall -Wextra -O2 -fopenmp fire_seq.c -o fire_seq

clean:
	rm -f fire_seq
