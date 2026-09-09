CC = gcc
CFLAGS = -O2 -Wall -Wextra -Wpedantic -std=c11 -fPIC
BUILD_DIR = .build
LIB = $(BUILD_DIR)/librand_helper.so

.PHONY: all clean

all: $(LIB)

$(LIB): rand_helper.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -shared rand_helper.c -o $(LIB)

clean:
	rm -rf $(BUILD_DIR)
