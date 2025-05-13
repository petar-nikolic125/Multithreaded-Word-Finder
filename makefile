# Compiler and flags
CC     := gcc
CFLAGS := \
  -D_POSIX_C_SOURCE=200809L \
  -D_XOPEN_SOURCE=700 \
  -Wall -Wextra -std=c11 -pedantic \
  -pthread -Iinclude

# Source files
SRC    := \
  src/main.c \
  src/job_queue.c \
  src/thread_pool.c \
  src/search_engine.c \
  src/util.c

# Object files & binary
OBJ    := $(SRC:.c=.o)
BIN    := search_engine

# Default target
all: $(BIN)

# Link step
$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Compile step
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Run tests
test: $(BIN)
	@tests/run_basic.sh
	@tests/run_concurrency.sh

# Clean up
clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all test clean
